#pragma once

#include <litebird/js/JsValue.hpp>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <exception>

namespace litebird {

class GeneratorAbortException : public std::exception {
public:
    const char* what() const noexcept override {
        return "Generator execution aborted";
    }
};

struct GeneratorData {
    std::mutex mtx;
    std::condition_variable cv_main;
    std::condition_variable cv_gen;

    bool started{false};
    bool waiting{false};
    bool finished{false};
    bool should_terminate{false};

    JsValue send_value{JsValue::make_undefined()};
    JsValue yielded_value{JsValue::make_undefined()};
    bool is_done{false};

    // An exception that escaped the generator body, to be rethrown in the
    // caller of next(); and one injected by throw(), to be raised at the
    // suspended `yield`.
    bool has_error{false};
    JsValue error{JsValue::make_undefined()};
    bool has_pending_throw{false};
    JsValue pending_throw{JsValue::make_undefined()};

    std::thread worker;

    ~GeneratorData() {
        terminate();
    }

    void terminate() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            should_terminate = true;
            waiting = false;
            cv_gen.notify_all();
        }
        if (worker.joinable()) {
            worker.join();
        }
    }

    JsValue yield_to_main(JsValue val) {
        std::unique_lock<std::mutex> lock(mtx);
        yielded_value = val;
        waiting = true;
        cv_main.notify_one();

        cv_gen.wait(lock, [this] {
            return !waiting || should_terminate;
        });

        if (should_terminate) {
            throw GeneratorAbortException();
        }

        return send_value;
    }

    JsValue step(JsValue send_arg) {
        if (is_done) {
            JsValue res = JsValue::make_object();
            res.set_property("value", JsValue::make_undefined());
            res.set_property("done", JsValue::make_boolean(true));
            return res;
        }

        std::unique_lock<std::mutex> lock(mtx);
        send_value = send_arg;

        if (!started) {
            started = true;
            cv_gen.notify_one();
        } else {
            waiting = false;
            cv_gen.notify_one();
        }

        cv_main.wait(lock, [this] {
            return waiting || finished;
        });

        JsValue res = JsValue::make_object();
        res.set_property("value", yielded_value);
        res.set_property("done", JsValue::make_boolean(is_done));
        return res;
    }
};

class GeneratorContext {
public:
    static GeneratorData* get_active();
    static void set_active(GeneratorData* gen);
};

} // namespace litebird
