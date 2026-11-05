#include "litebird/js/Promise.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Heap.hpp"
#include <iostream>

namespace litebird {

void Promise::resolve(const std::shared_ptr<PromiseData>& data, JsValue val) {
    if (!data || data->state != PromiseState::Pending) return;

    if (val.type == JsType::Object) {
        JsValue then_fn = val.get_property("then");
        if (then_fn.is_truthy()) {
            JsValue on_ful = JsValue::make_native_function([data](const std::vector<JsValue>& args) {
                JsValue inner_val = !args.empty() ? args[0] : JsValue::make_undefined();
                Promise::resolve(data, inner_val);
                return JsValue::make_undefined();
            });
            JsValue on_rej = JsValue::make_native_function([data](const std::vector<JsValue>& args) {
                JsValue err = !args.empty() ? args[0] : JsValue::make_undefined();
                Promise::reject(data, err);
                return JsValue::make_undefined();
            });
            then_fn.call({on_ful, on_rej});
            return;
        }
    }

    data->state = PromiseState::Fulfilled;
    data->result = std::move(val);

    auto handlers = std::move(data->handlers);
    data->handlers.clear();

    for (const auto& h : handlers) {
        JsValue on_fulfilled = h.on_fulfilled;
        JsValue cur_res = data->result;
        auto next = h.next_promise;

        EventLoop::default_loop().post_microtask([on_fulfilled, cur_res, next]() {
            if (on_fulfilled.type == JsType::Function || on_fulfilled.type == JsType::NativeFunction) {
                try {
                    JsValue r = on_fulfilled.call({cur_res});
                    if (next) {
                        Promise::resolve(next, r);
                    }
                } catch (const std::exception& ex) {
                    if (next) {
                        Promise::reject(next, JsValue::make_string(ex.what()));
                    }
                }
            } else {
                if (next) {
                    Promise::resolve(next, cur_res);
                }
            }
        });
    }
}

void Promise::reject(const std::shared_ptr<PromiseData>& data, JsValue reason) {
    if (!data || data->state != PromiseState::Pending) return;

    data->state = PromiseState::Rejected;
    data->result = std::move(reason);

    auto handlers = std::move(data->handlers);
    data->handlers.clear();

    for (const auto& h : handlers) {
        JsValue on_rejected = h.on_rejected;
        JsValue cur_res = data->result;
        auto next = h.next_promise;

        EventLoop::default_loop().post_microtask([on_rejected, cur_res, next]() {
            if (on_rejected.type == JsType::Function || on_rejected.type == JsType::NativeFunction) {
                try {
                    JsValue r = on_rejected.call({cur_res});
                    if (next) {
                        Promise::resolve(next, r);
                    }
                } catch (const std::exception& ex) {
                    if (next) {
                        Promise::reject(next, JsValue::make_string(ex.what()));
                    }
                }
            } else {
                if (next) {
                    Promise::reject(next, cur_res);
                }
            }
        });
    }
}

JsValue Promise::create_promise_instance(std::shared_ptr<PromiseData> data) {
    JsValue p = JsValue::make_object();
    p.set_property("__is_promise__", JsValue::make_boolean(true));

    p.set_property("then", JsValue::make_native_function([data](const std::vector<JsValue>& args) {
        auto next_data = std::make_shared<PromiseData>();
        JsValue on_fulfilled = !args.empty() ? args[0] : JsValue::make_undefined();
        JsValue on_rejected = args.size() > 1 ? args[1] : JsValue::make_undefined();

        if (data->state == PromiseState::Pending) {
            data->handlers.push_back({on_fulfilled, on_rejected, next_data});
        } else if (data->state == PromiseState::Fulfilled) {
            JsValue cur_res = data->result;
            EventLoop::default_loop().post_microtask([on_fulfilled, cur_res, next_data]() {
                if (on_fulfilled.type == JsType::Function || on_fulfilled.type == JsType::NativeFunction) {
                    try {
                        JsValue r = on_fulfilled.call({cur_res});
                        Promise::resolve(next_data, r);
                    } catch (const std::exception& ex) {
                        Promise::reject(next_data, JsValue::make_string(ex.what()));
                    }
                } else {
                    Promise::resolve(next_data, cur_res);
                }
            });
        } else if (data->state == PromiseState::Rejected) {
            JsValue cur_res = data->result;
            EventLoop::default_loop().post_microtask([on_rejected, cur_res, next_data]() {
                if (on_rejected.type == JsType::Function || on_rejected.type == JsType::NativeFunction) {
                    try {
                        JsValue r = on_rejected.call({cur_res});
                        Promise::resolve(next_data, r);
                    } catch (const std::exception& ex) {
                        Promise::reject(next_data, JsValue::make_string(ex.what()));
                    }
                } else {
                    Promise::reject(next_data, cur_res);
                }
            });
        }

        return Promise::create_promise_instance(next_data);
    }));

    p.set_property("catch", JsValue::make_native_function([data](const std::vector<JsValue>& args) {
        auto next_data = std::make_shared<PromiseData>();
        JsValue on_rejected = !args.empty() ? args[0] : JsValue::make_undefined();

        if (data->state == PromiseState::Pending) {
            data->handlers.push_back({JsValue::make_undefined(), on_rejected, next_data});
        } else if (data->state == PromiseState::Fulfilled) {
            JsValue cur_res = data->result;
            EventLoop::default_loop().post_microtask([cur_res, next_data]() {
                Promise::resolve(next_data, cur_res);
            });
        } else if (data->state == PromiseState::Rejected) {
            JsValue cur_res = data->result;
            EventLoop::default_loop().post_microtask([on_rejected, cur_res, next_data]() {
                if (on_rejected.type == JsType::Function || on_rejected.type == JsType::NativeFunction) {
                    try {
                        JsValue r = on_rejected.call({cur_res});
                        Promise::resolve(next_data, r);
                    } catch (const std::exception& ex) {
                        Promise::reject(next_data, JsValue::make_string(ex.what()));
                    }
                } else {
                    Promise::reject(next_data, cur_res);
                }
            });
        }

        return Promise::create_promise_instance(next_data);
    }));

    return p;
}

JsValue Promise::make_resolved(JsValue val) {
    auto data = std::make_shared<PromiseData>();
    data->state = PromiseState::Fulfilled;
    data->result = std::move(val);
    return create_promise_instance(data);
}

JsValue Promise::make_rejected(JsValue reason) {
    auto data = std::make_shared<PromiseData>();
    data->state = PromiseState::Rejected;
    data->result = std::move(reason);
    return create_promise_instance(data);
}

JsValue Promise::create_constructor() {
    JsValue ctor = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        auto data = std::make_shared<PromiseData>();
        JsValue p = Promise::create_promise_instance(data);

        if (!args.empty() && (args[0].type == JsType::Function || args[0].type == JsType::NativeFunction)) {
            JsValue executor = args[0];

            JsValue resolve_fn = JsValue::make_native_function([data](const std::vector<JsValue>& r_args) {
                JsValue val = !r_args.empty() ? r_args[0] : JsValue::make_undefined();
                Promise::resolve(data, val);
                return JsValue::make_undefined();
            });

            JsValue reject_fn = JsValue::make_native_function([data](const std::vector<JsValue>& r_args) {
                JsValue reason = !r_args.empty() ? r_args[0] : JsValue::make_undefined();
                Promise::reject(data, reason);
                return JsValue::make_undefined();
            });

            try {
                executor.call({resolve_fn, reject_fn});
            } catch (const std::exception& ex) {
                Promise::reject(data, JsValue::make_string(ex.what()));
            }
        }
        return p;
    });

    ctor.set_property("resolve", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        JsValue val = !args.empty() ? args[0] : JsValue::make_undefined();
        return Promise::make_resolved(val);
    }));

    ctor.set_property("reject", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        JsValue reason = !args.empty() ? args[0] : JsValue::make_undefined();
        return Promise::make_rejected(reason);
    }));

    ctor.set_property("all", JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty() || !args[0].is_array()) {
            return Promise::make_resolved(JsValue::make_array({}));
        }

        const auto& list = args[0].object_val->array_elements;
        size_t total = list.size();
        if (total == 0) {
            return Promise::make_resolved(JsValue::make_array({}));
        }

        auto all_data = std::make_shared<PromiseData>();
        auto results = std::make_shared<std::vector<JsValue>>(total, JsValue::make_undefined());
        auto remaining = std::make_shared<size_t>(total);

        for (size_t i = 0; i < total; ++i) {
            JsValue item = list[i];
            JsValue item_promise;

            // If item has a .then method, wrap or use it
            if (item.type == JsType::Object && item.get_property("then").is_truthy()) {
                item_promise = item;
            } else {
                item_promise = Promise::make_resolved(item);
            }

            JsValue then_fn = item_promise.get_property("then");
            if (then_fn.is_truthy()) {
                JsValue on_ful = JsValue::make_native_function([i, results, remaining, all_data](const std::vector<JsValue>& res_args) {
                    (*results)[i] = !res_args.empty() ? res_args[0] : JsValue::make_undefined();
                    (*remaining)--;
                    if (*remaining == 0) {
                        Promise::resolve(all_data, JsValue::make_array(*results));
                    }
                    return JsValue::make_undefined();
                });

                JsValue on_rej = JsValue::make_native_function([all_data](const std::vector<JsValue>& err_args) {
                    JsValue err = !err_args.empty() ? err_args[0] : JsValue::make_undefined();
                    Promise::reject(all_data, err);
                    return JsValue::make_undefined();
                });

                then_fn.call({on_ful, on_rej});
            }
        }

        return Promise::create_promise_instance(all_data);
    }));

    return ctor;
}

} // namespace litebird
