// URL and URLSearchParams as scripts see them.
//
// The parsing is in net/UrlParser.cpp. What is here is the two interfaces of
// the URL Standard: a URL object whose parts can be read and assigned, and
// the list of name/value pairs that is its query. `url.searchParams` and the
// URL are two views of one thing: changing either changes the other.

#include "litebird/js/DomBindings.hpp"
#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/JsString.hpp"
#include "litebird/net/UrlParser.hpp"

#include <algorithm>
#include <optional>

namespace litebird {
namespace dom {

namespace {

using Pairs = std::vector<std::pair<std::string, std::string>>;

// What a URL object and its searchParams share.
struct Shared {
    Pairs list;
    /// Absent for a URLSearchParams that was made on its own.
    std::optional<Url> url;
};

struct UrlState : JsExotic {
    std::shared_ptr<Shared> shared{std::make_shared<Shared>()};
};

struct ParamsState : JsExotic {
    std::shared_ptr<Shared> shared{std::make_shared<Shared>()};
};

// USVString: a string in which every surrogate half has become U+FFFD.
std::string to_usv(const JsValue& value) {
    const JsString text = js_to_js_string(value);
    return text.is_well_formed() ? text.str() : text.to_well_formed().str();
}

Url& this_url() {
    return *this_state<UrlState>("URL").shared->url;
}

Shared& this_params() {
    return *this_state<ParamsState>("URLSearchParams").shared;
}

// The list changed: the URL it belongs to follows.
void update_url(Shared& shared) {
    if (!shared.url) return;
    const std::string query = serialize_urlencoded(shared.list);
    shared.url->has_query = !query.empty();
    shared.url->query = query;
}

// The URL changed as a whole: the list is read from its query again.
void update_list(Shared& shared) {
    shared.list = shared.url->has_query ? parse_urlencoded(shared.url->query) : Pairs();
}

// `new URL(url, base)`, `URL.parse`, `URL.canParse`: the URL the arguments
// name, if they name one.
bool parse_arguments(Args args, Url& out) {
    const std::string input = to_usv(js_arg(args, 0));
    if (js_arg(args, 1).is_undefined()) return Url::parse(input, nullptr, out);
    Url base;
    if (!Url::parse(to_usv(args[1]), nullptr, base)) return false;
    return Url::parse(input, &base, out);
}

JsValue make_url(Realm& realm, Url url, const JsValue* derived = nullptr) {
    auto state = std::make_shared<UrlState>();
    state->shared->url = std::move(url);
    update_list(*state->shared);
    return realm.make("URL", state, derived);
}

JsValue construct_url(Realm& realm, const InterfaceDef&, Args args, const JsValue* derived) {
    require_args(args, 1, "URL");
    Url url;
    if (!parse_arguments(args, url)) js_throw_type_error("Invalid URL: " + to_usv(args[0]));
    return make_url(realm, std::move(url), derived);
}

void define_part(Iface& I, const char* name, std::string (Url::*get)() const, void (Url::*set)(std::string_view)) {
    I.accessor(name,
        [get](Realm&) { return str((this_url().*get)()); },
        [set](Realm&, const JsValue& value) {
            Url& url = this_url();
            (url.*set)(to_usv(value));
        });
}

void setup_url(Iface& I) {
    I.static_method("canParse", 1, [](Realm&, Args args) {
        require_args(args, 1, "URL.canParse");
        Url url;
        return boolean(parse_arguments(args, url));
    });
    I.static_method("parse", 1, [](Realm& r, Args args) {
        require_args(args, 1, "URL.parse");
        Url url;
        return parse_arguments(args, url) ? make_url(r, std::move(url)) : null();
    });

    I.accessor("href",
        [](Realm&) { return str(this_url().href()); },
        [](Realm&, const JsValue& value) {
            Shared& shared = *this_state<UrlState>("URL").shared;
            Url url;
            const std::string input = to_usv(value);
            if (!Url::parse(input, nullptr, url)) js_throw_type_error("Invalid URL: " + input);
            shared.url = std::move(url);
            update_list(shared);
        });
    I.getter("origin", [](Realm&) { return str(this_url().origin()); });
    define_part(I, "protocol", &Url::protocol, &Url::set_protocol);
    I.accessor("username",
        [](Realm&) { return str(this_url().username); },
        [](Realm&, const JsValue& value) { this_url().set_username(to_usv(value)); });
    I.accessor("password",
        [](Realm&) { return str(this_url().password); },
        [](Realm&, const JsValue& value) { this_url().set_password(to_usv(value)); });
    define_part(I, "host", &Url::host_and_port, &Url::set_host);
    define_part(I, "hostname", &Url::hostname, &Url::set_hostname);
    define_part(I, "port", &Url::port_text, &Url::set_port);
    define_part(I, "pathname", &Url::pathname, &Url::set_pathname);
    I.accessor("search",
        [](Realm&) { return str(this_url().search()); },
        [](Realm&, const JsValue& value) {
            Shared& shared = *this_state<UrlState>("URL").shared;
            std::string input = to_usv(value);
            shared.url->set_search(input);
            // The list is read from what was assigned, not from what the URL
            // made of it.
            if (!input.empty() && input[0] == '?') input.erase(0, 1);
            shared.list = parse_urlencoded(input);
        });
    I.getter("searchParams", [](Realm& r) {
        const JsValue self = JsValue::current_this();
        UrlState& state = this_state<UrlState>("URL");
        return cached(self, "searchParams", [&] {
            auto params = std::make_shared<ParamsState>();
            params->shared = state.shared;
            return r.make("URLSearchParams", params);
        });
    });
    define_part(I, "hash", &Url::hash, &Url::set_hash);
    I.method("toString", 0, [](Realm&, Args) { return str(this_url().href()); });
    I.method("toJSON", 0, [](Realm&, Args) { return str(this_url().href()); });
}

// --- URLSearchParams -----------------------------------------------------------------

JsValue construct_params(Realm& realm, const InterfaceDef&, Args args, const JsValue* derived) {
    auto state = std::make_shared<ParamsState>();
    Pairs& list = state->shared->list;
    const JsValue& init = js_arg(args, 0);
    if (init.is_undefined()) {
        // Nothing to start from.
    } else if (!init.is_object_like()) {
        // A query string, with or without its question mark.
        std::string text = to_usv(init);
        if (!text.empty() && text[0] == '?') text.erase(0, 1);
        list = parse_urlencoded(text);
    } else if (!init.get_property(kIteratorKey).is_nullish()) {
        // Anything that yields pairs: an array of arrays, a Map, a FormData,
        // another URLSearchParams.
        for (const JsValue& pair : js_iterable_to_vector(init)) {
            if (!pair.is_object_like()) {
                js_throw_type_error("Failed to construct 'URLSearchParams': each pair must be a sequence of two items.");
            }
            const std::vector<JsValue> parts = js_iterable_to_vector(pair);
            if (parts.size() != 2) {
                js_throw_type_error("Failed to construct 'URLSearchParams': each pair must have exactly two items.");
            }
            list.emplace_back(to_usv(parts[0]), to_usv(parts[1]));
        }
    } else {
        // A record: the object's own enumerable properties, in their order.
        // The same name twice cannot happen, but two names can become the
        // same string once surrogate halves are replaced: the later wins.
        for (const std::string& key : init.own_keys()) {
            const std::string name = to_usv(str(key));
            std::string value = to_usv(init.get_property(key));
            auto existing = std::find_if(list.begin(), list.end(), [&](const auto& entry) { return entry.first == name; });
            if (existing != list.end()) existing->second = std::move(value);
            else list.emplace_back(name, std::move(value));
        }
    }
    return realm.make("URLSearchParams", state, derived);
}

void setup_params(Iface& I) {
    I.getter("size", [](Realm&) { return num(static_cast<double>(this_params().list.size())); });
    I.method("append", 2, [](Realm&, Args args) {
        require_args(args, 2, "URLSearchParams.append");
        Shared& shared = this_params();
        shared.list.emplace_back(to_usv(args[0]), to_usv(args[1]));
        update_url(shared);
        return undefined();
    });
    I.method("delete", 1, [](Realm&, Args args) {
        require_args(args, 1, "URLSearchParams.delete");
        Shared& shared = this_params();
        const std::string name = to_usv(args[0]);
        const bool with_value = !js_arg(args, 1).is_undefined();
        const std::string value = with_value ? to_usv(args[1]) : std::string();
        shared.list.erase(std::remove_if(shared.list.begin(), shared.list.end(),
                                         [&](const auto& entry) {
                                             return entry.first == name && (!with_value || entry.second == value);
                                         }),
                          shared.list.end());
        update_url(shared);
        return undefined();
    });
    I.method("get", 1, [](Realm&, Args args) {
        require_args(args, 1, "URLSearchParams.get");
        const std::string name = to_usv(args[0]);
        for (const auto& entry : this_params().list) {
            if (entry.first == name) return str(entry.second);
        }
        return null();
    });
    I.method("getAll", 1, [](Realm&, Args args) {
        require_args(args, 1, "URLSearchParams.getAll");
        const std::string name = to_usv(args[0]);
        std::vector<JsValue> out;
        for (const auto& entry : this_params().list) {
            if (entry.first == name) out.push_back(str(entry.second));
        }
        return JsValue::make_array(out);
    });
    I.method("has", 1, [](Realm&, Args args) {
        require_args(args, 1, "URLSearchParams.has");
        const std::string name = to_usv(args[0]);
        const bool with_value = !js_arg(args, 1).is_undefined();
        const std::string value = with_value ? to_usv(args[1]) : std::string();
        for (const auto& entry : this_params().list) {
            if (entry.first == name && (!with_value || entry.second == value)) return boolean(true);
        }
        return boolean(false);
    });
    I.method("set", 2, [](Realm&, Args args) {
        require_args(args, 2, "URLSearchParams.set");
        Shared& shared = this_params();
        const std::string name = to_usv(args[0]);
        const std::string value = to_usv(args[1]);
        // The first pair of that name takes the value; the others go.
        bool placed = false;
        for (auto it = shared.list.begin(); it != shared.list.end();) {
            if (it->first != name) {
                ++it;
            } else if (!placed) {
                it->second = value;
                placed = true;
                ++it;
            } else {
                it = shared.list.erase(it);
            }
        }
        if (!placed) shared.list.emplace_back(name, value);
        update_url(shared);
        return undefined();
    });
    I.method("sort", 0, [](Realm&, Args) {
        Shared& shared = this_params();
        // By name, in the order strings compare in script; pairs of one name
        // stay as they were.
        std::stable_sort(shared.list.begin(), shared.list.end(),
                         [](const auto& a, const auto& b) { return JsString::compare(a.first, b.first) < 0; });
        update_url(shared);
        return undefined();
    });
    I.method("toString", 0, [](Realm&, Args) { return str(serialize_urlencoded(this_params().list)); });

    // Iteration reads the list as it is when each step is taken, so a pair
    // appended during a loop is visited.
    auto make_iterator = [](int what) {
        return [what](Realm&, Args) -> JsValue {
            const JsValue self = JsValue::current_this();
            std::shared_ptr<Shared> shared = this_state<ParamsState>("URLSearchParams").shared;
            auto index = std::make_shared<std::size_t>(0);
            return js_make_iterator([shared, index, what](JsValue& value) {
                if (*index >= shared->list.size()) return false;
                const auto& entry = shared->list[(*index)++];
                if (what == 1) value = str(entry.first);
                else if (what == 2) value = str(entry.second);
                else value = JsValue::make_array({str(entry.first), str(entry.second)});
                return true;
            }, {self});
        };
    };
    I.method("entries", 0, make_iterator(0));
    I.method("keys", 0, make_iterator(1));
    I.method("values", 0, make_iterator(2));
    define_method_value(I.proto, kIteratorKey, make_function(I.realm, "entries", 0, make_iterator(0)), PropertyMap::Hidden);
    I.method("forEach", 1, [](Realm&, Args args) {
        require_args(args, 1, "URLSearchParams.forEach");
        const JsValue self = JsValue::current_this();
        std::shared_ptr<Shared> shared = this_state<ParamsState>("URLSearchParams").shared;
        if (!args[0].is_function()) js_throw_type_error("URLSearchParams.forEach: parameter 1 is not a function.");
        for (std::size_t i = 0; i < shared->list.size(); ++i) {
            const auto entry = shared->list[i];             // a copy: the callback may change the list
            js_call(args[0], js_arg(args, 1), {str(entry.second), str(entry.first), self});
        }
        return undefined();
    });
}

} // namespace

void register_url_interfaces() {
    register_interface({"URL", nullptr, setup_url, construct_url, 1, "webkitURL"});
    register_interface({"URLSearchParams", nullptr, setup_params, construct_params, 0, nullptr});
}

} // namespace dom
} // namespace litebird
