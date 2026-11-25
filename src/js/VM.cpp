#include "litebird/js/VM.hpp"
#include "litebird/common/TerminalText.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/js/IterationCursor.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Promise.hpp"
#include "litebird/js/Json.hpp"
#include "litebird/js/Storage.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include "litebird/js/DomBindings.hpp"
#include "litebird/net/Fetch.hpp"
#include "litebird/net/WebSocket.hpp"
#include "litebird/js/Worker.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace litebird {
// ECMA-262 §7.3.24, the same routine the tree-walker's object literal and
// object rest destructuring call. Declared here rather than in JsOps.hpp
// because that header is another open pull request's (#366); the definition
// stays in JsEval.cpp.
void copy_data_properties(JsValue& target, const JsValue& source,
                          const std::vector<std::string>* excluded);


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

    // As in the tree-walker console, filter page-controlled text only at
    // the output boundary; the program-owned ANSI prefixes are unchanged.
    JsValue console_obj = JsValue::make_object();
    console_obj.set_property("log", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        for (size_t i = 0; i < args.size(); ++i) {
            std::cout << terminal_safe(js_output_text(args[i].to_string())) << (i + 1 < args.size() ? " " : "");
        }
        std::cout << std::endl;
        return JsValue::make_undefined();
    }));
    console_obj.set_property("warn", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        std::cout << "\033[33m[WARN] ";
        for (const auto& a : args) std::cout << terminal_safe(js_output_text(a.to_string())) << " ";
        std::cout << "\033[0m" << std::endl;
        return JsValue::make_undefined();
    }));
    console_obj.set_property("error", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        std::cerr << "\033[31m[ERROR] ";
        for (const auto& a : args) std::cerr << terminal_safe(js_output_text(a.to_string())) << " ";
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
    // A prototype can be a function as well as an object, and a function has
    // a prototype: the same three calls the interpreter's Object makes
    // (JsValue::as_prototype, prototype_object, js_set_prototype).
    object_obj.set_property("create", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        JsValue obj = JsValue::make_object();
        if (!args.empty() && args[0].is_object_like()) {
            if (JsObject* proto = args[0].as_prototype()) obj.object_val->prototype = proto;
        }
        return obj;
    }));
    object_obj.set_property("getPrototypeOf", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (!args.empty() && args[0].is_object_like()) {
            if (JsObject* proto = args[0].prototype_object()) return JsValue::make_object(proto);
        }
        return JsValue::make_null();
    }));
    object_obj.set_property("setPrototypeOf", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.size() >= 2 && args[0].is_object_like()) {
            JsObject* target = args[0].is_function() ? args[0].function_cell() : args[0].property_cell();
            // A link that would close a loop is not made (js_set_prototype says no).
            if (target && args[1].is_object_like()) {
                js_set_prototype(target, args[1].as_prototype());
            } else if (target && args[1].type == JsType::Null) {
                js_set_prototype(target, nullptr);
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

    // fetch, Headers, Response built-ins. The bytecode VM has no window of
    // its own; its document address is the origin, and there is no <base>.
    auto document = m_document;
    auto request_context = [document]() -> ResourceRequestContext {
        ResourceRequestContext context;
        context.document_url = document ? document->url() : "about:blank";
        context.base_url = context.document_url;
        return context;
    };
    set_global("Headers", HeadersObject::create_constructor());
    set_global("Response", ResponseObject::create_constructor());
    set_global("Request", RequestObject::create_constructor(request_context));
    set_global("fetch", JsValue::make_native_function(
        [request_context](const std::vector<JsValue>& args) {
            return FetchAPI::fetch(args, request_context());
        }));

    // URL & URLSearchParams built-ins
    set_global("URLSearchParams", dom::current_realm().ctor("URLSearchParams"));
    set_global("URL", dom::current_realm().ctor("URL"));

    // Worker built-in
    set_global("Worker", WorkerObject::create_constructor(request_context));

    // WebSocket built-in
    auto ws_context = [document]() -> WebSocketContext {
        WebSocketContext ctx;
        if (!document) {
            ctx.document_url = "about:blank";
            ctx.base_url = "about:blank";
            ctx.opaque_origin = true;
            return ctx;
        }
        ctx.opaque_origin = document->has_opaque_origin();
        ctx.document_url = document->url();
        if ((ctx.document_url == "about:srcdoc" || ctx.document_url == "about:blank") &&
            !document->inherited_origin_url().empty() && !document->has_opaque_origin()) {
            ctx.document_url = document->inherited_origin_url();
        }
        ctx.base_url = ctx.document_url;
        return ctx;
    };
    set_global("WebSocket", WebSocketObject::create_constructor(ws_context));

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
    m_iterators.clear();

    push(JsValue::make_undefined()); // slot 0 for main
    CallFrame frame;
    frame.function = function.get();
    frame.ip = function->chunk.code.data();
    frame.slots_offset = 0;
    m_frames.push_back(frame);

    // An abrupt completion (a default expression that throws, a getter, a
    // `return` that fails the close check) unwinds out of `run()` without
    // walking past the iterator any more than the tree-walker's bind_pattern
    // does: every cursor still open is told it will not be drained, quietly,
    // before the exception leaves the machine. `close_quietly` swallows what
    // the close itself says, which is the abrupt-completion rule (7.4.11).
    try {
        return run();
    } catch (...) {
        while (!m_iterators.empty()) {
            auto cursor = std::move(m_iterators.back());
            m_iterators.pop_back();
            // IteratorStep's own throwing `next` does not trigger close;
            // enclosing patterns still close as their binding was abrupt.
            if (!cursor->next_threw()) cursor->close_quietly();
        }
        m_stack.clear();
        m_frames.clear();
        throw;
    }
}

// Native abstract operations such as IteratorStep call JavaScript functions
// through JsValue. Re-enter the same VM, preserving the suspended frame and
// its iterators, instead of running a compiled function in the AST environment.
JsValue VM::call_compiled(const JsValue& function, const std::vector<JsValue>& args,
                          const JsValue* this_val) {
    const auto& code = function.function_bytecode();
    if (!code || !running()) throw std::runtime_error("No active bytecode function");
    if (m_frames.size() >= 64) throw std::runtime_error("Stack overflow (Call frame limit 64 exceeded)");
    const size_t depth = m_frames.size();
    const size_t stack_size = m_stack.size();
    const size_t iterator_count = m_iterators.size();
    push(function);
    for (const auto& arg : args) push(arg);
    m_stack.resize(stack_size + 1 + static_cast<size_t>(code->arity), JsValue::make_undefined());
    m_frames.push_back({code.get(), code->chunk.code.data(), stack_size});
    const JsValue saved_this = get_global("this");
    if (this_val && !this_val->is_nullish()) set_global("this", *this_val);
    try {
        JsValue result = run(depth);
        set_global("this", saved_this);
        return result;
    } catch (...) {
        set_global("this", saved_this);
        while (m_iterators.size() > iterator_count) {
            auto cursor = std::move(m_iterators.back());
            m_iterators.pop_back();
            if (!cursor->next_threw()) cursor->close_quietly();
        }
        m_frames.resize(depth);
        m_stack.resize(stack_size);
        throw;
    }
}

JsValue VM::run(size_t stop_depth) {
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
        // Collection stress, served at a bytecode boundary: the stack holds
        // every live value the VM owns, and the heap counts those handles, so
        // a collection here must free only what the script dropped. See
        // Heap::set_gc_stress.
        if (Heap::default_heap().take_gc_stress_request()) {
            collect_garbage();
        }
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
            case OpCode::OpDup:
                push(peek(0));
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
                const std::string name = read_constant().string_val.str();
                auto it = m_globals.find(name);
                if (it != m_globals.end()) {
                    push(it->second);
                } else {
                    // The quiet answer, for the operand of `typeof` only
                    // (§13.5.3 forbids that from throwing).
                    push(JsValue::make_undefined());
                }
                break;
            }
            case OpCode::OpGetGlobalChecked: {
                const std::string name = read_constant().string_val.str();
                auto it = m_globals.find(name);
                if (it != m_globals.end()) {
                    push(it->second);
                } else {
                    // ECMA-262 §6.2.5.5: GetValue on an unresolvable Reference is
                    // a `ReferenceError`, which is what the tree-walker raises
                    // (`JsEval.cpp`) and what this pushes `undefined` for
                    // before Phase 440.
                    js_throw_error(JsErrorKind::ReferenceError, name + " is not defined");
                }
                break;
            }
            case OpCode::OpSetGlobal: {
                const std::string name = read_constant().string_val.str();
                m_globals[name] = peek(0);
                break;
            }
            case OpCode::OpGetProperty: {
                const std::string prop = read_constant().string_val.str();
                JsValue obj = pop();
                JsValue value = obj.get_property(prop);
                // OpCall has no receiver operand, and built-in methods now read
                // `this`; bind it here so `array.push(x)` still works on the VM.
                if (value.is_function() && !obj.is_nullish()) value = js_bind_this(value, obj);
                push(std::move(value));
                break;
            }
            case OpCode::OpSetProperty: {
                const std::string prop = read_constant().string_val.str();
                JsValue obj = pop();
                JsValue val = peek(0);
                obj.set_property(prop, val);
                break;
            }
            case OpCode::OpGetIndex: {
                JsValue idx = pop();
                JsValue obj = pop();
                // Symbol.iterator is a symbol, not its display string.
                std::string prop = js_to_property_key(idx);
                push(obj.get_property(prop));
                break;
            }
            case OpCode::OpSetIndex: {
                JsValue idx = pop();
                JsValue obj = pop();
                JsValue val = peek(0);
                // Symbol.iterator is a symbol, not its display string.
                std::string prop = js_to_property_key(idx);
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
                    entries[i] = {key.string_val.str(), std::move(val)};
                }
                for (auto& [k, v] : entries) {
                    obj.set_property(k, std::move(v));
                }
                push(obj);
                break;
            }
            case OpCode::OpObjectDefine: {
                JsValue value = pop();
                JsValue key = pop();
                // The object being built sits below the operands; it stays on
                // the stack for the rest of the literal (peek is const-only).
                JsValue& target = m_stack.back();
                target.define_property(key.string_val.str(), std::move(value), PropertyMap::Default);
                break;
            }
            case OpCode::OpObjectSpread: {
                JsValue source = pop();
                // As with OpObjectDefine, the object under construction is
                // the value just below the source.
                JsValue& target = m_stack.back();
                copy_data_properties(target, source, nullptr);
                break;
            }
            case OpCode::OpDefineTemplateRaw: {
                JsValue raw = pop();
                // The tagged template's strings array sits below the raw one;
                // `raw` is not enumerable (PropertyMap::Hidden), matching the
                // tree-walker's eval_tagged_template.
                JsValue& strings = m_stack.back();
                strings.define_property("raw", std::move(raw), PropertyMap::Hidden);
                break;
            }

            case OpCode::OpIteratorInit: {
                JsValue source = pop();
                // A non-iterable here throws the same TypeError the
                // tree-walker's bind_pattern throws — same constructor call,
                // same message. It never reaches the inside of the cursor.
                m_iterators.push_back(std::make_unique<IterationCursor>(source));
                break;
            }
            case OpCode::OpIteratorNext: {
                if (m_iterators.empty()) throw std::runtime_error("OP_ITERATOR_NEXT without OP_ITERATOR_INIT");
                push(m_iterators.back()->pull());
                break;
            }
            case OpCode::OpIteratorDrain: {
                if (m_iterators.empty()) throw std::runtime_error("OP_ITERATOR_DRAIN without OP_ITERATOR_INIT");
                std::vector<JsValue> rest;
                while (true) {
                    JsValue item = m_iterators.back()->pull();
                    if (m_iterators.back()->done()) break;
                    rest.push_back(std::move(item));
                }
                push(JsValue::make_array(std::move(rest)));
                break;
            }
            case OpCode::OpIteratorClose: {
                if (m_iterators.empty()) throw std::runtime_error("OP_ITERATOR_CLOSE without OP_ITERATOR_INIT");
                // The normal way out reports a broken `return` (a throwing
                // one, a non-callable one, a non-object answer), exactly as the
                // tree-walker's bind_pattern does after the last element.
                m_iterators.back()->close();
                m_iterators.pop_back();
                break;
            }
            case OpCode::OpIsUndefined:
                push(JsValue::make_boolean(peek(0).is_undefined()));
                break;

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
            case OpCode::OpStrictEqual: {
                JsValue b = pop();
                JsValue a = pop();
                push(JsValue::make_boolean(strict_equals(a, b)));
                break;
            }
            case OpCode::OpStrictNotEqual: {
                JsValue b = pop();
                JsValue a = pop();
                push(JsValue::make_boolean(!strict_equals(a, b)));
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
            case OpCode::OpTypeOf: {
                // The same function the tree-walker asks, so the two answers
                // cannot drift apart for any value that reaches both.
                JsValue val = pop();
                push(js_typeof_value(val));
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

                if (callee.type == JsType::NativeFunction && callee.native_function()) {
                    std::vector<JsValue> args;
                    args.reserve(arg_count);
                    for (int i = arg_count - 1; i >= 0; --i) {
                        args.push_back(peek(i));
                    }
                    JsValue result = callee.native_function()(args);
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
                    // Slots after the callee are parameters; ensure missing
                    // arguments and extra arguments do not shift local slots.
                    m_stack.resize(next_frame.slots_offset + 1 + static_cast<size_t>(sub_fn->arity),
                                   JsValue::make_undefined());
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
                if (m_stack.size() > return_slot) {
                    m_stack.resize(return_slot);
                }
                if (m_frames.size() == stop_depth) {
                    return result;
                }

                push(result);
                frame = &m_frames.back();
                break;
            }
        }
    }
}

} // namespace litebird
