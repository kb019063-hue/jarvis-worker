#include "litebird/js/VM.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Promise.hpp"
#include "litebird/js/Json.hpp"
#include "litebird/js/Storage.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include "litebird/net/URL.hpp"
#include "litebird/net/Fetch.hpp"
#include "litebird/net/WebSocket.hpp"
#include "litebird/js/Worker.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace litebird {

VM::VM(std::shared_ptr<Document> doc) : m_document(std::move(doc)) {
    m_stack.reserve(2048);
    m_frames.reserve(64);
    init_builtins();
}

void VM::set_document(std::shared_ptr<Document> doc) {
    m_document = std::move(doc);
    init_builtins();
}

void VM::set_global(const std::string& name, JsValue value) {
    m_globals[name] = std::move(value);
}

JsValue VM::get_global(const std::string& name) const {
    auto it = m_globals.find(name);
    return it != m_globals.end() ? it->second : JsValue::make_undefined();
}

void VM::push(JsValue value) {
    m_stack.push_back(std::move(value));
}

JsValue VM::pop() {
    if (m_stack.empty()) {
        return JsValue::make_undefined();
    }
    JsValue val = std::move(m_stack.back());
    m_stack.pop_back();
    return val;
}

const JsValue& VM::peek(size_t distance) const {
    if (distance >= m_stack.size()) {
        static JsValue undef = JsValue::make_undefined();
        return undef;
    }
    return m_stack[m_stack.size() - 1 - distance];
}

GCStats VM::collect_garbage() {
    std::vector<JsValue*> roots;
    roots.reserve(m_stack.size() + m_globals.size());
    for (auto& v : m_stack) {
        roots.push_back(&v);
    }
    for (auto& [k, v] : m_globals) {
        roots.push_back(&v);
    }
    return Heap::default_heap().collect_garbage(roots);
}

void VM::init_builtins() {
    // gc function
    set_global("gc", JsValue::make_native_function([this](const std::vector<JsValue>&) {
        auto stats = collect_garbage();
        JsValue res = JsValue::make_object();
        res.set_property("total_cells", JsValue::make_number(stats.total_cells));
        res.set_property("alive_cells", JsValue::make_number(stats.alive_cells));
        res.set_property("collected_cells", JsValue::make_number(stats.collected_cells));
        res.set_property("bytes_allocated", JsValue::make_number(stats.bytes_allocated));
        res.set_property("collections_count", JsValue::make_number(stats.collections_count));
        return res;
    }));

    // console object
    JsValue console_obj = JsValue::make_object();
    console_obj.set_property("log", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        for (size_t i = 0; i < args.size(); ++i) {
            std::cout << args[i].to_string() << (i + 1 < args.size() ? " " : "");
        }
        std::cout << std::endl;
        return JsValue::make_undefined();
    }));
    console_obj.set_property("warn", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        std::cout << "\033[33m[WARN] ";
        for (const auto& a : args) std::cout << a.to_string() << " ";
        std::cout << "\033[0m" << std::endl;
        return JsValue::make_undefined();
    }));
    console_obj.set_property("error", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        std::cerr << "\033[31m[ERROR] ";
        for (const auto& a : args) std::cerr << a.to_string() << " ";
        std::cerr << "\033[0m" << std::endl;
        return JsValue::make_undefined();
    }));
    set_global("console", console_obj);

    // Math object
    JsValue math_obj = JsValue::make_object();
    math_obj.set_property("PI", JsValue::make_number(3.141592653589793));
    math_obj.set_property("floor", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        return JsValue::make_number(args.empty() ? 0.0 : std::floor(args[0].to_number()));
    }));
    math_obj.set_property("ceil", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        return JsValue::make_number(args.empty() ? 0.0 : std::ceil(args[0].to_number()));
    }));
    math_obj.set_property("round", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        return JsValue::make_number(args.empty() ? 0.0 : std::round(args[0].to_number()));
    }));
    math_obj.set_property("abs", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        return JsValue::make_number(args.empty() ? 0.0 : std::abs(args[0].to_number()));
    }));
    set_global("Math", math_obj);

    // Web Storage API
    JsValue local_store = Storage::local_storage().to_js_value();
    JsValue session_store = Storage::session_storage().to_js_value();
    set_global("localStorage", local_store);
    set_global("sessionStorage", session_store);

    JsValue win_obj = JsValue::make_object();
    win_obj.set_property("localStorage", local_store);
    win_obj.set_property("sessionStorage", session_store);

    JsValue raf = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_number(0);
        JsValue callback = args[0];
        uint32_t id = EventLoop::default_loop().set_timeout([callback]() {
            callback.call({JsValue::make_number(16.0)});
        }, 16);
        return JsValue::make_number(id);
    });
    set_global("requestAnimationFrame", raf);
    win_obj.set_property("requestAnimationFrame", raf);

    set_global("window", win_obj);

    // document object
    if (m_document) {
        JsValue doc_obj = JsValue::make_object();
        doc_obj.set_property("title", JsValue::make_string(m_document->title()));
        doc_obj.set_property("cookie", JsValue::make_string(CookieJar::default_jar().get_document_cookie()));
        doc_obj.set_property("getElementById", JsValue::make_native_function([this](const std::vector<JsValue>& args) {
            if (args.empty() || !m_document) return JsValue::make_null();
            auto el = m_document->get_element_by_id(args[0].to_string());
            if (el) {
                return JsValue::make_dom_node(el);
            }
            return JsValue::make_null();
        }));
        set_global("document", doc_obj);
    }

    // setTimeout(callback, delay)
    set_global("setTimeout", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_number(0);
        JsValue callback = args[0];
        int delay = (args.size() > 1) ? static_cast<int>(args[1].to_number()) : 0;
        uint32_t id = EventLoop::default_loop().set_timeout([callback]() {
            callback.call({});
        }, delay);
        return JsValue::make_number(id);
    }));

    // setInterval(callback, interval)
    set_global("setInterval", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_number(0);
        JsValue callback = args[0];
        int interval = (args.size() > 1) ? static_cast<int>(args[1].to_number()) : 0;
        uint32_t id = EventLoop::default_loop().set_interval([callback]() {
            callback.call({});
        }, interval);
        return JsValue::make_number(id);
    }));

    // clearTimeout(id)
    set_global("clearTimeout", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (!args.empty()) {
            EventLoop::default_loop().clear_timer(static_cast<uint32_t>(args[0].to_number()));
        }
        return JsValue::make_undefined();
    }));

    // clearInterval(id)
    set_global("clearInterval", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (!args.empty()) {
            EventLoop::default_loop().clear_timer(static_cast<uint32_t>(args[0].to_number()));
        }
        return JsValue::make_undefined();
    }));

    // Object built-ins
    JsValue object_obj = JsValue::make_object();
    object_obj.set_property("create", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        JsValue obj = JsValue::make_object();
        if (!args.empty() && args[0].type == JsType::Object && args[0].object_val) {
            obj.object_val->prototype = args[0].object_val;
        }
        return obj;
    }));
    object_obj.set_property("getPrototypeOf", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (!args.empty() && args[0].type == JsType::Object && args[0].object_val && args[0].object_val->prototype) {
            return JsValue::make_object(args[0].object_val->prototype);
        }
        return JsValue::make_null();
    }));
    object_obj.set_property("setPrototypeOf", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.size() >= 2 && args[0].type == JsType::Object && args[0].object_val) {
            if (args[1].type == JsType::Object && args[1].object_val) {
                args[0].object_val->prototype = args[1].object_val;
            } else if (args[1].type == JsType::Null) {
                args[0].object_val->prototype = nullptr;
            }
        }
        return args.empty() ? JsValue::make_undefined() : args[0];
    }));
    set_global("Object", object_obj);

    // Array built-ins
    JsValue array_ctor = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        return JsValue::make_array(args);
    });
    array_ctor.set_property("isArray", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (!args.empty()) {
            return JsValue::make_boolean(args[0].is_array());
        }
        return JsValue::make_boolean(false);
    }));
    set_global("Array", array_ctor);

    // Promise built-in
    set_global("Promise", Promise::create_constructor());

    // JSON built-in
    JsValue json_obj = JsValue::make_object();
    json_obj.set_property("parse", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_undefined();
        try {
            return Json::parse(args[0].to_string());
        } catch (const std::exception& ex) {
            std::cerr << "JSON.parse Error: " << ex.what() << std::endl;
            return JsValue::make_null();
        }
    }));
    json_obj.set_property("stringify", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_undefined();
        return JsValue::make_string(Json::stringify(args[0]));
    }));
    set_global("JSON", json_obj);

    // fetch, Headers, Response built-ins
    set_global("Headers", HeadersObject::create_constructor());
    set_global("Response", ResponseObject::create_constructor());
    set_global("fetch", JsValue::make_native_function(FetchAPI::fetch));

    // URL & URLSearchParams built-ins
    set_global("URLSearchParams", URLSearchParamsObject::create_constructor());
    set_global("URL", URLObject::create_constructor());

    // Worker built-in
    set_global("Worker", WorkerObject::create_constructor());

    // WebSocket built-in
    set_global("WebSocket", WebSocketObject::create_constructor());

    // Map Constructor
    JsValue map_ctor = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        JsValue map_inst = JsValue::make_object();
        auto map_store = std::make_shared<std::vector<std::pair<std::string, JsValue>>>();

        map_inst.set_property("__get_size__", JsValue::make_native_function([map_store](const std::vector<JsValue>&) {
            return JsValue::make_number(map_store->size());
        }));

        map_inst.set_property("set", JsValue::make_native_function([map_inst, map_store](const std::vector<JsValue>& sargs) {
            if (!sargs.empty()) {
                std::string k = sargs[0].to_string();
                JsValue v = (sargs.size() > 1) ? sargs[1] : JsValue::make_undefined();
                bool found = false;
                for (auto& pair : *map_store) {
                    if (pair.first == k) {
                        pair.second = v;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    map_store->emplace_back(k, v);
                }
            }
            return map_inst;
        }));

        map_inst.set_property("get", JsValue::make_native_function([map_store](const std::vector<JsValue>& gargs) {
            if (gargs.empty()) return JsValue::make_undefined();
            std::string k = gargs[0].to_string();
            for (const auto& pair : *map_store) {
                if (pair.first == k) return pair.second;
            }
            return JsValue::make_undefined();
        }));

        map_inst.set_property("has", JsValue::make_native_function([map_store](const std::vector<JsValue>& hargs) {
            if (hargs.empty()) return JsValue::make_boolean(false);
            std::string k = hargs[0].to_string();
            for (const auto& pair : *map_store) {
                if (pair.first == k) return JsValue::make_boolean(true);
            }
            return JsValue::make_boolean(false);
        }));

        map_inst.set_property("delete", JsValue::make_native_function([map_store](const std::vector<JsValue>& dargs) {
            if (dargs.empty()) return JsValue::make_boolean(false);
            std::string k = dargs[0].to_string();
            for (auto it = map_store->begin(); it != map_store->end(); ++it) {
                if (it->first == k) {
                    map_store->erase(it);
                    return JsValue::make_boolean(true);
                }
            }
            return JsValue::make_boolean(false);
        }));

        map_inst.set_property("clear", JsValue::make_native_function([map_store](const std::vector<JsValue>&) {
            map_store->clear();
            return JsValue::make_undefined();
        }));

        if (!args.empty() && args[0].is_array()) {
            JsValue arr = args[0];
            double len = arr.get_property("length").to_number();
            for (size_t i = 0; i < static_cast<size_t>(len); ++i) {
                JsValue entry = arr.get_property(std::to_string(i));
                if (entry.is_array()) {
                    std::string ek = entry.get_property("0").to_string();
                    JsValue ev = entry.get_property("1");
                    map_store->emplace_back(ek, ev);
                }
            }
        }

        return map_inst;
    });
    set_global("Map", map_ctor);

    // Set Constructor
    JsValue set_ctor = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        JsValue set_inst = JsValue::make_object();
        auto set_store = std::make_shared<std::vector<std::string>>();

        set_inst.set_property("__get_size__", JsValue::make_native_function([set_store](const std::vector<JsValue>&) {
            return JsValue::make_number(set_store->size());
        }));

        set_inst.set_property("add", JsValue::make_native_function([set_inst, set_store](const std::vector<JsValue>& sargs) {
            if (!sargs.empty()) {
                std::string k = sargs[0].to_string();
                if (std::find(set_store->begin(), set_store->end(), k) == set_store->end()) {
                    set_store->push_back(k);
                }
            }
            return set_inst;
        }));

        set_inst.set_property("has", JsValue::make_native_function([set_store](const std::vector<JsValue>& hargs) {
            if (hargs.empty()) return JsValue::make_boolean(false);
            std::string k = hargs[0].to_string();
            bool found = (std::find(set_store->begin(), set_store->end(), k) != set_store->end());
            return JsValue::make_boolean(found);
        }));

        set_inst.set_property("delete", JsValue::make_native_function([set_store](const std::vector<JsValue>& dargs) {
            if (dargs.empty()) return JsValue::make_boolean(false);
            std::string k = dargs[0].to_string();
            auto it = std::find(set_store->begin(), set_store->end(), k);
            if (it != set_store->end()) {
                set_store->erase(it);
                return JsValue::make_boolean(true);
            }
            return JsValue::make_boolean(false);
        }));

        set_inst.set_property("clear", JsValue::make_native_function([set_store](const std::vector<JsValue>&) {
            set_store->clear();
            return JsValue::make_undefined();
        }));

        if (!args.empty() && args[0].is_array()) {
            JsValue arr = args[0];
            double len = arr.get_property("length").to_number();
            for (size_t i = 0; i < static_cast<size_t>(len); ++i) {
                std::string val = arr.get_property(std::to_string(i)).to_string();
                if (std::find(set_store->begin(), set_store->end(), val) == set_store->end()) {
                    set_store->push_back(val);
                }
            }
        }

        return set_inst;
    });
    set_global("Set", set_ctor);
}

JsValue VM::interpret(std::shared_ptr<BytecodeFunction> function) {
    if (!function) return JsValue::make_undefined();

    m_stack.clear();
    m_frames.clear();

    push(JsValue::make_undefined()); // slot 0 for main
    CallFrame frame;
    frame.function = function.get();
    frame.ip = function->chunk.code.data();
    frame.slots_offset = 0;
    m_frames.push_back(frame);

    return run();
}

JsValue VM::run() {
    CallFrame* frame = &m_frames.back();

    auto read_byte = [&frame]() -> uint8_t {
        return *frame->ip++;
    };

    auto read_short = [&frame]() -> uint16_t {
        frame->ip += 2;
        return (static_cast<uint16_t>(frame->ip[-2]) << 8) | frame->ip[-1];
    };

    auto read_constant = [&frame, &read_byte]() -> const JsValue& {
        uint8_t idx = read_byte();
        return frame->function->chunk.constants[idx];
    };

    while (true) {
        uint8_t instruction = read_byte();
        auto op = static_cast<OpCode>(instruction);

        switch (op) {
            case OpCode::OpConstant: {
                push(read_constant());
                break;
            }
            case OpCode::OpNull:
                push(JsValue::make_null());
                break;
            case OpCode::OpUndefined:
                push(JsValue::make_undefined());
                break;
            case OpCode::OpTrue:
                push(JsValue::make_boolean(true));
                break;
            case OpCode::OpFalse:
                push(JsValue::make_boolean(false));
                break;
            case OpCode::OpPop:
                pop();
                break;

            case OpCode::OpGetLocal: {
                uint8_t slot = read_byte();
                push(m_stack[frame->slots_offset + slot]);
                break;
            }
            case OpCode::OpSetLocal: {
                uint8_t slot = read_byte();
                m_stack[frame->slots_offset + slot] = peek(0);
                break;
            }
            case OpCode::OpGetGlobal: {
                const auto& name = read_constant().string_val;
                auto it = m_globals.find(name);
                if (it != m_globals.end()) {
                    push(it->second);
                } else {
                    push(JsValue::make_undefined());
                }
                break;
            }
            case OpCode::OpSetGlobal: {
                const auto& name = read_constant().string_val;
                m_globals[name] = peek(0);
                break;
            }
            case OpCode::OpGetProperty: {
                const auto& prop = read_constant().string_val;
                JsValue obj = pop();
                push(obj.get_property(prop));
                break;
            }
            case OpCode::OpSetProperty: {
                const auto& prop = read_constant().string_val;
                JsValue obj = pop();
                JsValue val = peek(0);
                obj.set_property(prop, val);
                break;
            }
            case OpCode::OpGetIndex: {
                JsValue idx = pop();
                JsValue obj = pop();
                std::string prop = (idx.type == JsType::Number) ?
                    std::to_string(static_cast<int64_t>(idx.number_val)) : idx.to_string();
                push(obj.get_property(prop));
                break;
            }
            case OpCode::OpSetIndex: {
                JsValue idx = pop();
                JsValue obj = pop();
                JsValue val = peek(0);
                std::string prop = (idx.type == JsType::Number) ?
                    std::to_string(static_cast<int64_t>(idx.number_val)) : idx.to_string();
                obj.set_property(prop, val);
                break;
            }
            case OpCode::OpNewArray: {
                uint8_t count = read_byte();
                std::vector<JsValue> elems(count);
                for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
                    elems[i] = pop();
                }
                push(JsValue::make_array(elems));
                break;
            }
            case OpCode::OpNewObject: {
                uint8_t count = read_byte();
                JsValue obj = JsValue::make_object();
                std::vector<std::pair<std::string, JsValue>> entries(count);
                for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
                    JsValue val = pop();
                    JsValue key = pop();
                    entries[i] = {key.string_val, std::move(val)};
                }
                for (auto& [k, v] : entries) {
                    obj.set_property(k, std::move(v));
                }
                push(obj);
                break;
            }

            case OpCode::OpAdd: {
                JsValue b = pop();
                JsValue a = pop();
                if (a.type == JsType::String || b.type == JsType::String) {
                    push(JsValue::make_string(a.to_string() + b.to_string()));
                } else {
                    push(JsValue::make_number(a.to_number() + b.to_number()));
                }
                break;
            }
            case OpCode::OpSubtract: {
                JsValue b = pop();
                JsValue a = pop();
                push(JsValue::make_number(a.to_number() - b.to_number()));
                break;
            }
            case OpCode::OpMultiply: {
                JsValue b = pop();
                JsValue a = pop();
                push(JsValue::make_number(a.to_number() * b.to_number()));
                break;
            }
            case OpCode::OpDivide: {
                JsValue b = pop();
                JsValue a = pop();
                push(JsValue::make_number(a.to_number() / b.to_number()));
                break;
            }
            case OpCode::OpModulo: {
                JsValue b = pop();
                JsValue a = pop();
                push(JsValue::make_number(std::fmod(a.to_number(), b.to_number())));
                break;
            }
            case OpCode::OpEqual: {
                JsValue b = pop();
                JsValue a = pop();
                if (a.type == b.type) {
                    if (a.type == JsType::Number) push(JsValue::make_boolean(a.number_val == b.number_val));
                    else if (a.type == JsType::String) push(JsValue::make_boolean(a.string_val == b.string_val));
                    else if (a.type == JsType::Boolean) push(JsValue::make_boolean(a.bool_val == b.bool_val));
                    else push(JsValue::make_boolean(a.to_string() == b.to_string()));
                } else {
                    push(JsValue::make_boolean(a.to_string() == b.to_string()));
                }
                break;
            }
            case OpCode::OpNotEqual: {
                JsValue b = pop();
                JsValue a = pop();
                push(JsValue::make_boolean(a.to_string() != b.to_string()));
                break;
            }
            case OpCode::OpGreater: {
                JsValue b = pop();
                JsValue a = pop();
                push(JsValue::make_boolean(a.to_number() > b.to_number()));
                break;
            }
            case OpCode::OpGreaterEqual: {
                JsValue b = pop();
                JsValue a = pop();
                push(JsValue::make_boolean(a.to_number() >= b.to_number()));
                break;
            }
            case OpCode::OpLess: {
                JsValue b = pop();
                JsValue a = pop();
                push(JsValue::make_boolean(a.to_number() < b.to_number()));
                break;
            }
            case OpCode::OpLessEqual: {
                JsValue b = pop();
                JsValue a = pop();
                push(JsValue::make_boolean(a.to_number() <= b.to_number()));
                break;
            }
            case OpCode::OpNot: {
                JsValue val = pop();
                push(JsValue::make_boolean(!val.is_truthy()));
                break;
            }
            case OpCode::OpNegate: {
                JsValue val = pop();
                push(JsValue::make_number(-val.to_number()));
                break;
            }

            case OpCode::OpJump: {
                uint16_t offset = read_short();
                frame->ip += offset;
                break;
            }
            case OpCode::OpJumpIfFalse: {
                uint16_t offset = read_short();
                if (!peek(0).is_truthy()) {
                    frame->ip += offset;
                }
                break;
            }
            case OpCode::OpLoop: {
                uint16_t offset = read_short();
                frame->ip -= offset;
                break;
            }

            case OpCode::OpCall: {
                uint8_t arg_count = read_byte();
                JsValue callee = peek(arg_count);

                if (callee.type == JsType::NativeFunction && callee.native_fn) {
                    std::vector<JsValue> args;
                    args.reserve(arg_count);
                    for (int i = arg_count - 1; i >= 0; --i) {
                        args.push_back(peek(i));
                    }
                    JsValue result = callee.native_fn(args);
                    // Pop arguments and callee
                    for (size_t i = 0; i <= arg_count; ++i) pop();
                    push(std::move(result));
                } else if (callee.type == JsType::Function) {
                    auto fn_ptr_val = callee.get_property("__fn_ptr__");
                    auto* sub_fn = reinterpret_cast<const BytecodeFunction*>(static_cast<uintptr_t>(fn_ptr_val.number_val));

                    if (m_frames.size() >= 64) {
                        throw std::runtime_error("Stack overflow (Call frame limit 64 exceeded)");
                    }

                    CallFrame next_frame;
                    next_frame.function = sub_fn;
                    next_frame.ip = sub_fn->chunk.code.data();
                    next_frame.slots_offset = m_stack.size() - arg_count - 1;
                    m_frames.push_back(next_frame);
                    frame = &m_frames.back();
                } else {
                    throw std::runtime_error("Called non-callable object: " + callee.to_string());
                }
                break;
            }

            case OpCode::OpReturn: {
                JsValue result = pop();
                size_t return_slot = frame->slots_offset;

                m_frames.pop_back();
                if (m_frames.empty()) {
                    return result;
                }

                if (m_stack.size() > return_slot) {
                    m_stack.resize(return_slot);
                }

                push(result);
                frame = &m_frames.back();
                break;
            }
        }
    }
}

} // namespace litebird
