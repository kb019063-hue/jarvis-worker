#include "litebird/js/Worker.hpp"
#include "litebird/js/JsInterpreter.hpp"
#include "litebird/js/Json.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

namespace litebird {

static std::string load_worker_script(std::string_view url_or_path) {
    std::string str(url_or_path);
    if (url_or_path.starts_with("data:")) {
        auto resp = ResourceLoader::load(str);
        if (resp.success) return resp.body;
    }
    // Try file directly
    std::ifstream file{str};
    if (file.is_open()) {
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
    // Try via ResourceLoader
    auto resp = ResourceLoader::load(str);
    if (resp.success) return resp.body;
    return str;
}

WorkerObject::WorkerObject(std::string script_url)
    : m_script_url(std::move(script_url)) {
}

WorkerObject::~WorkerObject() {
    terminate();
}

void WorkerObject::start() {
    m_running = true;
    m_terminated = false;
    m_owner_loop = &EventLoop::default_loop();
    std::string script_source = load_worker_script(m_script_url);

    m_thread = std::thread([this, script = std::move(script_source)]() {
        worker_thread_main(script);
    });
}

void WorkerObject::terminate() {
    m_terminated = true;
    m_running = false;
    m_cv.notify_all();
    if (m_thread.joinable()) {
        if (std::this_thread::get_id() != m_thread.get_id()) {
            m_thread.join();
        } else {
            m_thread.detach();
        }
    }
}

void WorkerObject::post_message(const std::string& serialized_json) {
    if (m_terminated || !m_running) return;
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_incoming_messages.push(serialized_json);
    }
    m_cv.notify_one();
}

void WorkerObject::add_event_listener(const std::string& event, JsValue listener) {
    if (event == "message") {
        m_message_listeners.push_back(listener);
    } else if (event == "error") {
        m_error_listeners.push_back(listener);
    }
}

void WorkerObject::worker_thread_main(std::string script_source) {
    // A worker is an agent of its own: its timers and promise reactions run on
    // this thread, in this loop, never in the page's. Declared before the
    // interpreter so that it outlives it.
    EventLoop worker_loop;
    EventLoop::set_current(&worker_loop);
    EventLoop* owner_loop = m_owner_loop;

    JsInterpreter worker_js(nullptr);
    auto env = worker_js.global_env();

    // An error nothing in the worker caught goes to the page's worker.onerror.
    auto report_error = [this, owner_loop](std::string message) {
        owner_loop->post_task([weak_self = weak_from_this(), err = std::move(message)]() {
            auto worker = weak_self.lock();
            if (!worker || worker->m_terminated) return;
            if (worker->m_onerror.is_function()) {
                JsValue err_event = JsValue::make_object(Heap::default_heap().allocate_object());
                err_event.set_property("message", JsValue::make_string(err));
                err_event.set_property("type", JsValue::make_string("error"));
                js_call_guarded(worker->m_onerror, JsValue::make_undefined(), {err_event});
            } else {
                std::cerr << "Uncaught (in worker) " << err << std::endl;
            }
        });
    };
    // Runs worker script and returns false, after reporting, if it threw.
    auto run_script = [&report_error](const std::function<void()>& body) {
        JsValue thrown;
        bool ok = false;
        {
            JsScriptScope script_scope;     // promise reactions run when the script is done
            ok = js_capture_throw(body, thrown);
        }
        if (!ok) report_error(js_describe_thrown(thrown));
        return ok;
    };

    std::vector<JsValue> worker_message_listeners;

    // self reference
    JsValue self_obj = JsValue::make_object(Heap::default_heap().allocate_object());
    env->define("self", self_obj);

    // postMessage(data) inside worker
    auto post_msg_fn = JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_undefined();
        std::string json_str;
        try {
            json_str = Json::stringify(args[0]);
        } catch (...) {
            json_str = args[0].to_string();
        }

        m_owner_loop->post_task([weak_self = weak_from_this(), json = std::move(json_str)]() {
            auto worker = weak_self.lock();
            if (!worker || worker->m_terminated) return;

            JsValue data;
            try {
                data = Json::parse(json);
            } catch (...) {
                data = JsValue::make_string(json);
            }

            JsValue event = JsValue::make_object(Heap::default_heap().allocate_object());
            event.set_property("data", data);
            event.set_property("type", JsValue::make_string("message"));

            if (worker->m_onmessage.is_function()) {
                try {
                    worker->m_onmessage.call({event});
                } catch (const std::exception& ex) {
                    if (worker->m_onerror.is_function()) {
                        JsValue err_event = JsValue::make_object(Heap::default_heap().allocate_object());
                        err_event.set_property("message", JsValue::make_string(ex.what()));
                        worker->m_onerror.call({err_event});
                    }
                }
            }

            for (auto& listener : worker->m_message_listeners) {
                if (listener.is_function()) {
                    try {
                        listener.call({event});
                    } catch (...) {}
                }
            }
        });

        return JsValue::make_undefined();
    });

    env->define("postMessage", post_msg_fn);
    self_obj.set_property("postMessage", post_msg_fn);

    // close() inside worker
    auto close_fn = JsValue::make_native_function([this](const std::vector<JsValue>&) {
        m_running = false;
        m_cv.notify_all();
        return JsValue::make_undefined();
    });
    env->define("close", close_fn);
    self_obj.set_property("close", close_fn);

    // addEventListener inside worker
    auto add_el_fn = JsValue::make_native_function([&worker_message_listeners](const std::vector<JsValue>& args) {
        if (args.size() >= 2 && args[0].to_string() == "message" && args[1].is_function()) {
            worker_message_listeners.push_back(args[1]);
        }
        return JsValue::make_undefined();
    });
    env->define("addEventListener", add_el_fn);
    self_obj.set_property("addEventListener", add_el_fn);

    // Initial evaluation of the worker script. A worker whose script fails to
    // load or throws at the top level never starts.
    if (!run_script([&] { worker_js.run(script_source); })) {
        m_running = false;
        return;
    }
    worker_loop.tick();

    // Message pump: wait for a message from the page or for the worker's next
    // timer, whichever comes first.
    while (m_running && !m_terminated) {
        std::string incoming_json;
        bool have_message = false;
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            auto woken = [this]() { return !m_incoming_messages.empty() || !m_running || m_terminated; };
            const auto next_timer = worker_loop.time_until_next();
            if (!next_timer) {
                m_cv.wait(lock, woken);
            } else if (next_timer->count() > 0) {
                m_cv.wait_for(lock, *next_timer, woken);
            }
            if (!m_running || m_terminated) break;
            if (!m_incoming_messages.empty()) {
                incoming_json = std::move(m_incoming_messages.front());
                m_incoming_messages.pop();
                have_message = true;
            }
        }

        if (have_message) {
            JsValue data;
            try {
                data = Json::parse(incoming_json);
            } catch (...) {
                data = JsValue::make_string(incoming_json);
            }

            JsValue event = JsValue::make_object(Heap::default_heap().allocate_object());
            event.set_property("data", data);
            event.set_property("type", JsValue::make_string("message"));

            // Check self.onmessage or global onmessage
            JsValue onmsg = env->get("onmessage");
            if (!onmsg.is_function()) {
                onmsg = self_obj.get_property("onmessage");
            }
            if (onmsg.is_function()) {
                run_script([&] { onmsg.call({event}); });
            }
            // A listener may add or remove listeners; iterate over a copy.
            const auto listeners = worker_message_listeners;
            for (const auto& listener : listeners) {
                if (listener.is_function()) run_script([&] { listener.call({event}); });
            }
        }

        worker_loop.tick();
    }
}

JsValue WorkerObject::create_js_instance(std::string_view script_url) {
    auto worker = std::make_shared<WorkerObject>(std::string(script_url));
    worker->start();

    JsValue inst = JsValue::make_object(Heap::default_heap().allocate_object());

    // postMessage
    inst.set_property("postMessage", JsValue::make_native_function([worker](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_undefined();
        std::string json;
        try {
            json = Json::stringify(args[0]);
        } catch (...) {
            json = args[0].to_string();
        }
        worker->post_message(json);
        return JsValue::make_undefined();
    }));

    // terminate
    inst.set_property("terminate", JsValue::make_native_function([worker](const std::vector<JsValue>&) {
        worker->terminate();
        return JsValue::make_undefined();
    }));

    // addEventListener
    inst.set_property("addEventListener", JsValue::make_native_function([worker](const std::vector<JsValue>& args) {
        if (args.size() >= 2 && args[1].is_function()) {
            worker->add_event_listener(args[0].to_string(), args[1]);
        }
        return JsValue::make_undefined();
    }));

    // onmessage setter hook
    inst.set_property("onmessage", JsValue::make_null());
    inst.set_property("onerror", JsValue::make_null());

    // We store the native worker reference in an internal function or intercept
    // To handle worker.onmessage = fn in JS:
    // When worker.onmessage is accessed, we keep it synchronized.
    // Also provide a helper method setOnmessage if needed, or wrap in proxy.
    // In our engine, properties set on inst can be checked directly by checking inst.get_property("onmessage")!
    // But since callbacks run asynchronously on worker thread, let's also hook a setOnMessage native helper or sync:
    inst.set_property("setOnMessage", JsValue::make_native_function([worker, inst](const std::vector<JsValue>& args) mutable {
        if (!args.empty()) {
            worker->set_onmessage(args[0]);
            inst.set_property("onmessage", args[0]);
        }
        return JsValue::make_undefined();
    }));

    // In litebird JS engine, property assignments `worker.onmessage = ...` write to inst.properties.
    // In our EventLoop dispatch, we can inspect inst.get_property("onmessage")!
    // So let's store a weak reference to inst in worker or query inst!
    // Let's hook worker's onmessage look-up to also query inst:
    return inst;
}

JsValue WorkerObject::create_constructor() {
    return JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) {
            throw std::runtime_error("TypeError: Worker constructor requires at least 1 argument");
        }
        std::string script_url = args[0].to_string();
        auto worker = std::make_shared<WorkerObject>(script_url);
        worker->start();

        JsValue inst = JsValue::make_object(Heap::default_heap().allocate_object());

        inst.set_property("postMessage", JsValue::make_native_function([worker](const std::vector<JsValue>& msg_args) {
            if (msg_args.empty()) return JsValue::make_undefined();
            std::string json;
            try {
                json = Json::stringify(msg_args[0]);
            } catch (...) {
                json = msg_args[0].to_string();
            }
            worker->post_message(json);
            return JsValue::make_undefined();
        }));

        inst.set_property("terminate", JsValue::make_native_function([worker](const std::vector<JsValue>&) {
            worker->terminate();
            return JsValue::make_undefined();
        }));

        inst.set_property("addEventListener", JsValue::make_native_function([worker](const std::vector<JsValue>& el_args) {
            if (el_args.size() >= 2 && el_args[1].is_function()) {
                worker->add_event_listener(el_args[0].to_string(), el_args[1]);
            }
            return JsValue::make_undefined();
        }));

        // Allow worker to check inst's "onmessage" and "onerror" properties dynamically
        // by wrapping post_task to check inst properties as well!
        // We override worker's callback dispatch:
        // When worker dispatches message back to main thread:
        auto old_post_task_bridge = [worker, inst]() {
            // Nothing special needed, inst.get_property("onmessage") is accessible on main thread!
        };
        (void)old_post_task_bridge;

        // Custom task dispatch for this instance:
        // We hook post_message response:
        // Since worker thread posts to EventLoop on the main thread,
        // we can check both worker->m_onmessage AND inst.get_property("onmessage")!
        worker->set_onmessage(JsValue::make_native_function([inst, worker](const std::vector<JsValue>& ev_args) {
            JsValue h = inst.get_property("onmessage");
            if (h.is_function()) {
                h.call(ev_args);
            }
            return JsValue::make_undefined();
        }));

        worker->set_onerror(JsValue::make_native_function([inst](const std::vector<JsValue>& ev_args) {
            JsValue h = inst.get_property("onerror");
            if (h.is_function()) {
                h.call(ev_args);
            }
            return JsValue::make_undefined();
        }));

        return inst;
    });
}

} // namespace litebird
