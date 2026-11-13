#include "litebird/net/URL.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/js/JsBuiltins.hpp"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace litebird {

static std::string url_decode(std::string_view str, bool plus_as_space = true) {
    std::string res;
    res.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (plus_as_space && str[i] == '+') {
            res += ' ';
        } else if (str[i] == '%' && i + 2 < str.size()) {
            char h1 = str[i + 1];
            char h2 = str[i + 2];
            auto hex_val = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int v1 = hex_val(h1);
            int v2 = hex_val(h2);
            if (v1 >= 0 && v2 >= 0) {
                res += static_cast<char>((v1 << 4) | v2);
                i += 2;
            } else {
                res += str[i];
            }
        } else {
            res += str[i];
        }
    }
    return res;
}

static std::string url_encode(std::string_view str) {
    std::string res;
    for (char c : str) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            res += c;
        } else if (c == ' ') {
            res += '+';
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            res += buf;
        }
    }
    return res;
}

void URLSearchParamsObject::parse(std::string_view query) {
    entries.clear();
    if (query.empty()) return;
    if (query.front() == '?') query.remove_prefix(1);

    std::stringstream ss{std::string(query)};
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        if (pair.empty()) continue;
        auto eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string k = url_decode(pair.substr(0, eq));
            std::string v = url_decode(pair.substr(eq + 1));
            entries.emplace_back(std::move(k), std::move(v));
        } else {
            entries.emplace_back(url_decode(pair), "");
        }
    }
}

std::string URLSearchParamsObject::to_string() const {
    std::string res;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) res += '&';
        res += url_encode(entries[i].first);
        res += '=';
        res += url_encode(entries[i].second);
    }
    return res;
}

void URLSearchParamsObject::set(const std::string& name, const std::string& value) {
    bool found = false;
    std::vector<std::pair<std::string, std::string>> new_entries;
    for (const auto& p : entries) {
        if (p.first == name) {
            if (!found) {
                new_entries.emplace_back(name, value);
                found = true;
            }
        } else {
            new_entries.push_back(p);
        }
    }
    if (!found) {
        new_entries.emplace_back(name, value);
    }
    entries = std::move(new_entries);
    if (on_change) on_change();
}

void URLSearchParamsObject::append(const std::string& name, const std::string& value) {
    entries.emplace_back(name, value);
    if (on_change) on_change();
}

bool URLSearchParamsObject::has(const std::string& name) const {
    for (const auto& p : entries) {
        if (p.first == name) return true;
    }
    return false;
}

std::string URLSearchParamsObject::get(const std::string& name) const {
    for (const auto& p : entries) {
        if (p.first == name) return p.second;
    }
    return "";
}

std::vector<std::string> URLSearchParamsObject::get_all(const std::string& name) const {
    std::vector<std::string> res;
    for (const auto& p : entries) {
        if (p.first == name) res.push_back(p.second);
    }
    return res;
}

void URLSearchParamsObject::remove(const std::string& name) {
    entries.erase(
        std::remove_if(entries.begin(), entries.end(), [&](const auto& p) { return p.first == name; }),
        entries.end()
    );
    if (on_change) on_change();
}

JsValue URLSearchParamsObject::wrap_existing(std::shared_ptr<URLSearchParamsObject> self) {
    JsValue inst = JsValue::make_object(Heap::default_heap().allocate_object());

    inst.set_property("get", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_null();
        std::string name = args[0].to_string();
        if (!self->has(name)) return JsValue::make_null();
        return JsValue::make_string(self->get(name));
    }));

    inst.set_property("getAll", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_array({});
        auto all = self->get_all(args[0].to_string());
        std::vector<JsValue> js_elements;
        for (const auto& s : all) js_elements.push_back(JsValue::make_string(s));
        return JsValue::make_array(js_elements);
    }));

    inst.set_property("set", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            self->set(args[0].to_string(), args[1].to_string());
        }
        return JsValue::make_undefined();
    }));

    inst.set_property("append", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            self->append(args[0].to_string(), args[1].to_string());
        }
        return JsValue::make_undefined();
    }));

    inst.set_property("has", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (args.empty()) return JsValue::make_boolean(false);
        return JsValue::make_boolean(self->has(args[0].to_string()));
    }));

    inst.set_property("delete", JsValue::make_native_function([self](const std::vector<JsValue>& args) {
        if (!args.empty()) {
            self->remove(args[0].to_string());
        }
        return JsValue::make_undefined();
    }));

    inst.set_property("toString", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        return JsValue::make_string(self->to_string());
    }));

    // Iteration: `for (const [name, value] of params)`, entries(), keys(),
    // values(), forEach(). The iterators read the list as it is when each
    // step is taken, so a pair appended during a loop is visited.
    enum class Part { Entry, Key, Value };
    auto make_iterator = [self](Part part) {
        return JsValue::make_native_function([self, part](const std::vector<JsValue>&) {
            auto index = std::make_shared<size_t>(0);
            return js_make_iterator([self, part, index](JsValue& value) {
                if (*index >= self->entries.size()) return false;
                const auto& entry = self->entries[(*index)++];
                if (part == Part::Key) value = JsValue::make_string(entry.first);
                else if (part == Part::Value) value = JsValue::make_string(entry.second);
                else value = JsValue::make_array({JsValue::make_string(entry.first), JsValue::make_string(entry.second)});
                return true;
            });
        });
    };
    JsValue entries_fn = make_iterator(Part::Entry);
    inst.set_property("entries", entries_fn);
    inst.set_property("keys", make_iterator(Part::Key));
    inst.set_property("values", make_iterator(Part::Value));
    inst.set_property(kIteratorKey, entries_fn);
    inst.set_property("forEach", JsValue::make_native_function([self, inst_cell = inst.object_val](const std::vector<JsValue>& args) {
        if (args.empty() || !args[0].is_function()) {
            js_throw_error(JsErrorKind::TypeError, "URLSearchParams.forEach: the callback is not a function");
        }
        const JsValue this_arg = args.size() > 1 ? args[1] : JsValue::make_undefined();
        for (size_t i = 0; i < self->entries.size(); ++i) {
            const auto entry = self->entries[i];
            args[0].call_with_this(this_arg, {JsValue::make_string(entry.second), JsValue::make_string(entry.first),
                                              JsValue::make_object(inst_cell)});
        }
        return JsValue::make_undefined();
    }));
    inst.set_property("sort", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        std::stable_sort(self->entries.begin(), self->entries.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        if (self->on_change) self->on_change();
        return JsValue::make_undefined();
    }));
    // None of these show up when the object's properties are listed.
    for (const char* name : {"get", "getAll", "set", "append", "has", "delete", "toString", "entries", "keys",
                             "values", "forEach", "sort", kIteratorKey}) {
        inst.object_val->properties.entry(name).flags = PropertyMap::Hidden;
    }
    js_define_getter(inst.object_val, "size", [self](const std::vector<JsValue>&) {
        return JsValue::make_number(static_cast<double>(self->entries.size()));
    });

    return inst;
}

JsValue URLSearchParamsObject::create_js_instance(
    const std::string& init,
    std::function<void()> change_listener
) {
    auto self = std::make_shared<URLSearchParamsObject>();
    self->on_change = std::move(change_listener);
    self->parse(init);
    return wrap_existing(self);
}

JsValue URLSearchParamsObject::create_constructor() {
    return JsValue::make_native_function([](const std::vector<JsValue>& args) {
        auto self = std::make_shared<URLSearchParamsObject>();
        if (args.empty() || args[0].is_undefined()) return wrap_existing(self);
        const JsValue& init = args[0];
        if (!init.is_object_like() || init.is_function()) {
            // A query string, with or without its question mark.
            self->parse(init.to_string());
            return wrap_existing(self);
        }
        JsValue iterator = init.get_property(kIteratorKey);
        if (iterator.is_function()) {
            // Anything that yields pairs: an array of arrays, a Map, a
            // FormData, another URLSearchParams.
            js_iterate(init, [&](const JsValue& pair) {
                std::vector<std::string> parts;
                if (pair.is_object_like()) {
                    js_iterate(pair, [&](const JsValue& part) {
                        parts.push_back(js_to_string(part));
                        return parts.size() <= 2;
                    });
                }
                if (parts.size() != 2) {
                    js_throw_error(JsErrorKind::TypeError,
                                   "URLSearchParams: each pair must be an iterable of exactly two items");
                }
                self->entries.emplace_back(std::move(parts[0]), std::move(parts[1]));
                return true;
            });
            return wrap_existing(self);
        }
        // A record: the object's own enumerable properties.
        for (const std::string& key : init.own_keys()) {
            self->entries.emplace_back(key, js_to_string(init.get_property(key)));
        }
        return wrap_existing(self);
    });
}

// -------------------------------------------------------------
// URLObject implementation
// -------------------------------------------------------------

bool URLObject::parse(std::string_view url_str, std::string_view base_str) {
    std::string full_url;
    if (!base_str.empty()) {
        full_url = ResourceLoader::resolve_url(base_str, url_str);
    } else {
        full_url = std::string(url_str);
    }

    href = full_url;

    // 1. Protocol
    auto colon_slash = full_url.find("://");
    if (colon_slash != std::string::npos) {
        protocol = full_url.substr(0, colon_slash + 1);
        std::transform(protocol.begin(), protocol.end(), protocol.begin(), ::tolower);
    } else if (full_url.starts_with("data:")) {
        protocol = "data:";
    } else if (full_url.starts_with("file:")) {
        protocol = "file:";
    } else {
        protocol = "http:";
    }

    // 2. Find hash and search
    auto hash_pos = full_url.find('#');
    std::string without_hash = full_url;
    if (hash_pos != std::string::npos) {
        hash = full_url.substr(hash_pos);
        without_hash = full_url.substr(0, hash_pos);
    } else {
        hash = "";
    }

    auto search_pos = without_hash.find('?');
    std::string without_search_hash = without_hash;
    if (search_pos != std::string::npos) {
        search = without_hash.substr(search_pos);
        without_search_hash = without_hash.substr(0, search_pos);
    } else {
        search = "";
    }

    // 3. Authority and Pathname
    if (colon_slash != std::string::npos) {
        std::string after_scheme = without_search_hash.substr(colon_slash + 3);
        auto slash_pos = after_scheme.find('/');
        if (slash_pos != std::string::npos) {
            host = after_scheme.substr(0, slash_pos);
            pathname = after_scheme.substr(slash_pos);
        } else {
            host = after_scheme;
            pathname = "/";
        }

        origin = protocol + "//" + host;

        auto colon_in_host = host.find(':');
        if (colon_in_host != std::string::npos) {
            hostname = host.substr(0, colon_in_host);
            port = host.substr(colon_in_host + 1);
        } else {
            hostname = host;
            port = "";
        }
    } else {
        host = "";
        hostname = "";
        port = "";
        pathname = without_search_hash;
        origin = "null";
    }

    search_params = std::make_shared<URLSearchParamsObject>();
    search_params->parse(search);

    return true;
}

void URLObject::update_from_search_params() {
    if (search_params) {
        std::string qs = search_params->to_string();
        search = qs.empty() ? "" : ("?" + qs);
    }
    href = origin.empty() || origin == "null" ? pathname : (origin + pathname);
    href += search;
    href += hash;
}

void URLObject::update_search(const std::string& new_search) {
    search = new_search;
    if (search_params) {
        search_params->parse(search);
    }
    update_from_search_params();
}

JsValue URLObject::create_js_instance(std::string_view url_str, std::string_view base_str) {
    auto self = std::make_shared<URLObject>();
    self->parse(url_str, base_str);

    JsValue inst = JsValue::make_object(Heap::default_heap().allocate_object());

    auto sync_props = [self, inst]() mutable {
        inst.set_property("href", JsValue::make_string(self->href));
        inst.set_property("protocol", JsValue::make_string(self->protocol));
        inst.set_property("host", JsValue::make_string(self->host));
        inst.set_property("hostname", JsValue::make_string(self->hostname));
        inst.set_property("port", JsValue::make_string(self->port));
        inst.set_property("pathname", JsValue::make_string(self->pathname));
        inst.set_property("search", JsValue::make_string(self->search));
        inst.set_property("hash", JsValue::make_string(self->hash));
        inst.set_property("origin", JsValue::make_string(self->origin));
    };

    self->search_params->on_change = [self, sync_props]() mutable {
        self->update_from_search_params();
        sync_props();
    };

    JsValue sp_inst = URLSearchParamsObject::wrap_existing(self->search_params);

    sync_props();
    inst.set_property("searchParams", sp_inst);

    inst.set_property("toString", JsValue::make_native_function([self](const std::vector<JsValue>&) {
        return JsValue::make_string(self->href);
    }));

    return inst;
}

JsValue URLObject::create_constructor() {
    return JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) {
            throw std::runtime_error("TypeError: URL constructor requires at least 1 argument");
        }
        std::string url_str = args[0].to_string();
        std::string base_str = args.size() > 1 ? args[1].to_string() : "";
        return URLObject::create_js_instance(url_str, base_str);
    });
}

} // namespace litebird
