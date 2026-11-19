#include "litebird/js/Worker.hpp"
#include "litebird/js/DomBindings.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/js/JsInterpreter.hpp"
#include "litebird/js/JsLexer.hpp"
#include "litebird/js/JsOps.hpp"
#include "litebird/net/Fetch.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include "litebird/net/UrlParser.hpp"
#include "litebird/net/WebSocket.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace litebird {

struct WorkerOwnerLoopState {
    std::mutex mutex;
    EventLoop* loop{nullptr};
    std::function<void()> wake;
    bool alive{true};
};

namespace {

struct EventListenerEntry {
    JsValue callback;
    bool once{false};
};

struct ListenerMap {
    std::unordered_map<std::string, std::vector<EventListenerEntry>> by_type;

    void add(const std::string& type, const JsValue& listener, bool once) {
        if (!listener.is_function() && !listener.is_object_like()) return;
        auto& list = by_type[type];
        for (const auto& existing : list) {
            if (strict_equals(existing.callback, listener)) return;
        }
        list.push_back(EventListenerEntry{listener, once});
    }

    void remove(const std::string& type, const JsValue& listener) {
        auto it = by_type.find(type);
        if (it == by_type.end()) return;
        auto& list = it->second;
        for (auto entry = list.begin(); entry != list.end(); ++entry) {
            if (strict_equals(entry->callback, listener)) {
                list.erase(entry);
                return;
            }
        }
    }

    bool has(const std::string& type) const {
        auto it = by_type.find(type);
        return it != by_type.end() && !it->second.empty();
    }

    std::vector<EventListenerEntry> snapshot(const std::string& type) const {
        auto it = by_type.find(type);
        if (it == by_type.end()) return {};
        return it->second;
    }

    void clear() { by_type.clear(); }
};

struct EventControl {
    bool default_prevented{false};
    bool stopped{false};
    bool immediate_stopped{false};
};

bool read_once_option(const JsValue& arg) {
    if (!arg.is_object_like()) return false;
    return arg.get_property("once").is_truthy();
}

void invoke_event_listener(const JsValue& callback,
                           const JsValue& target,
                           const JsValue& event,
                           JsValue* thrown_out) {
    auto call_body = [&]() {
        if (callback.is_function()) {
            callback.call_with_this(target, {event});
        } else if (callback.is_object_like()) {
            JsValue handle_event = callback.get_property("handleEvent");
            if (handle_event.is_function()) {
                handle_event.call_with_this(callback, {event});
            }
        }
    };
    if (thrown_out) {
        JsValue thrown;
        if (!js_capture_throw(call_body, thrown)) {
            *thrown_out = std::move(thrown);
        }
    } else {
        JsValue ignored;
        (void)js_capture_throw(call_body, ignored);
    }
}

void attach_event_methods(JsValue& event,
                          const std::shared_ptr<EventControl>& control,
                          bool cancelable) {
    event.set_property("bubbles", JsValue::make_boolean(false));
    event.set_property("cancelable", JsValue::make_boolean(cancelable));
    event.set_property("defaultPrevented", JsValue::make_boolean(false));

    JsValue ev_ref = event;
    event.set_property("preventDefault", JsValue::make_native_function(
        [control, cancelable, ev_ref](const std::vector<JsValue>&) mutable {
            if (cancelable) {
                control->default_prevented = true;
                ev_ref.set_property("defaultPrevented", JsValue::make_boolean(true));
            }
            return JsValue::make_undefined();
        }));
    event.set_property("stopPropagation", JsValue::make_native_function(
        [control](const std::vector<JsValue>&) {
            control->stopped = true;
            return JsValue::make_undefined();
        }));
    event.set_property("stopImmediatePropagation", JsValue::make_native_function(
        [control](const std::vector<JsValue>&) {
            control->stopped = true;
            control->immediate_stopped = true;
            return JsValue::make_undefined();
        }));
}

bool is_http_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string extract_mime_essence(std::string_view content_type) {
    const size_t semi = content_type.find(';');
    std::string_view essence = (semi == std::string_view::npos) ? content_type : content_type.substr(0, semi);
    while (!essence.empty() && is_http_whitespace(essence.front())) essence.remove_prefix(1);
    while (!essence.empty() && is_http_whitespace(essence.back())) essence.remove_suffix(1);
    std::string lower;
    lower.reserve(essence.size());
    for (char c : essence) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lower;
}

std::string trim_header_value(std::string_view value) {
    while (!value.empty() && is_http_whitespace(value.front())) value.remove_prefix(1);
    while (!value.empty() && is_http_whitespace(value.back())) value.remove_suffix(1);
    return std::string(value);
}

std::string response_content_type(const ResourceResponse& resp) {
    if (!resp.content_type.empty()) return trim_header_value(resp.content_type);
    auto it = resp.headers.find("content-type");
    if (it != resp.headers.end()) return trim_header_value(it->second);
    return {};
}

std::optional<std::string> load_worker_script(
    std::string_view url_or_path,
    const ResourceRequestContext& context,
    RequestMode mode,
    std::string& error,
    std::string& loaded_url
) {
    // A worker script is a page-initiated load (Phase 33 local-file boundary
    // plus Phase 36 request modes: same-origin for a top-level classic worker,
    // CORS for module graph fetches, and no-cors for classic importScripts).
    ResourceRequestContext req_ctx = context;
    req_ctx.mode = mode;
    auto resp = ResourceLoader::load_for_page(std::string(url_or_path), req_ctx);
    if (!resp.url.empty()) loaded_url = resp.url;
    if (resp.status_code > 0 && (resp.status_code < 200 || resp.status_code >= 300)) {
        error = "Worker script request failed with HTTP status " + std::to_string(resp.status_code);
        return std::nullopt;
    }
    if (!resp.success) {
        error = resp.error_message.empty() ? "Failed to load worker script" : resp.error_message;
        return std::nullopt;
    }

    // HTML's worker-script fetches require a JavaScript MIME type (no MIME
    // sniffing), for both the classic path and this engine's module graph.
    const std::string ct = response_content_type(resp);
    if (ct.empty()) {
        error = "Refused to execute worker script: missing Content-Type header (expected a JavaScript MIME type)";
        return std::nullopt;
    }
    if (!WorkerObject::is_javascript_mime_type(ct)) {
        const std::string essence = extract_mime_essence(ct);
        error = "Refused to execute worker script: non-JavaScript MIME type '" +
                (essence.empty() ? ct : essence) + "'";
        return std::nullopt;
    }

    return resp.body;
}

// ---------------------------------------------------------------------------
// Small static ES-module graph loader for module workers
// ---------------------------------------------------------------------------
//
// The tree-walking parser deliberately parses classic programs only.  Module
// workers still need a module map and module-local top level, so this loader
// recognizes a conservative static-import/export subset before passing the
// ordinary statements through that parser inside a strict wrapper.  It is not
// an attempt to quietly treat a module as a classic script: unsupported forms
// fail before that module body executes, dependencies run before their
// importers, and each module has a private lexical wrapper.
//
// Supported forms are side-effect imports, default/named/namespace imports,
// `export` declarations, `export default`, local named export lists, and named
// re-exports. The engine does not yet have module-record/live-binding
// machinery, so bindings/re-exports are snapshots; `export *`, dynamic
// import(), import.meta, cycles, and top-level await are rejected explicitly
// rather than receiving subtly wrong semantics.

struct ModuleImportBinding {
    /// The exported name for an ordinary import. Empty for a namespace import.
    std::string imported_name;
    /// A valid local binding name (or a generated private one for a re-export).
    std::string local_name;
    /// Private key in the native-created import bag passed to the wrapper.
    std::string slot;
    bool namespace_import{false};
};

struct ModuleDependency {
    std::string specifier;
    std::vector<ModuleImportBinding> bindings;
};

struct ModuleExportBinding {
    std::string exported_name;
    std::string local_name;
};

struct ModuleSourceEdit {
    std::size_t begin{0};
    std::size_t end{0};
    std::string replacement;
};

struct ModuleTransform {
    std::string body;
    std::vector<ModuleDependency> dependencies;
    std::vector<ModuleExportBinding> exports;
};

bool module_contextual(const JsToken& token, std::string_view word) {
    return token.type == JsTokenType::Identifier && !token.has_escape && token.text == word;
}

bool module_binding_token(const JsToken& token) {
    // The wrapper is always strict. The parser will apply the remaining strict
    // binding checks (for example `eval`) to a normal Identifier token.
    return token.type == JsTokenType::Identifier;
}

bool module_export_name_token(const JsToken& token) {
    // This static subset accepts IdentifierName (including keywords such as
    // `default`) but deliberately leaves string-named imports/exports to a
    // future complete module grammar.
    return token.type == JsTokenType::Identifier || token.is_keyword;
}

[[noreturn]] void module_syntax_error(const JsToken& token, std::string_view detail) {
    throw JsSyntaxError("Unsupported ES module worker syntax: " + std::string(detail), token.line);
}

std::string module_token_name(const JsToken& token) {
    return token.text;
}

std::string module_fresh_identifier(const std::string& source,
                                    std::size_t& serial,
                                    std::string_view purpose) {
    // Avoid a collision even with a module that intentionally uses the usual
    // internal-looking prefix. A source-text check is conservative (comments
    // and strings also reserve a candidate) but keeps the wrapper unobservable.
    while (true) {
        std::string candidate = "__litebird_module_" + std::string(purpose) + "_" +
                                std::to_string(serial++);
        if (source.find(candidate) == std::string::npos) return candidate;
    }
}

std::string module_quote(std::string_view value) {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('\'');
    static constexpr char hex[] = "0123456789abcdef";
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\': quoted += "\\\\"; break;
            case '\'': quoted += "\\'"; break;
            case '\n': quoted += "\\n"; break;
            case '\r': quoted += "\\r"; break;
            case '\t': quoted += "\\t"; break;
            default:
                if (ch < 0x20) {
                    quoted += "\\x";
                    quoted.push_back(hex[(ch >> 4) & 0xf]);
                    quoted.push_back(hex[ch & 0xf]);
                } else {
                    quoted.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    quoted.push_back('\'');
    return quoted;
}

std::size_t module_statement_end(const std::vector<JsToken>& tokens, std::size_t next) {
    if (next >= tokens.size()) return next;
    if (tokens[next].type == JsTokenType::Semicolon) return next + 1;
    if (tokens[next].type == JsTokenType::Eof || tokens[next].newline_before) return next;
    module_syntax_error(tokens[next], "expected a semicolon or line break after module declaration");
}

void module_add_export(ModuleTransform& transform,
                       const JsToken& where,
                       std::string exported_name,
                       std::string local_name) {
    for (const auto& existing : transform.exports) {
        if (existing.exported_name == exported_name) {
            module_syntax_error(where, "duplicate export '" + exported_name + "'");
        }
    }
    transform.exports.push_back(ModuleExportBinding{std::move(exported_name), std::move(local_name)});
}

void module_add_import(ModuleTransform& transform,
                       const JsToken& where,
                       ModuleDependency& dependency,
                       std::string imported_name,
                       std::string local_name,
                       bool namespace_import,
                       std::size_t& slot_serial) {
    for (const auto& prior_dependency : transform.dependencies) {
        for (const auto& prior : prior_dependency.bindings) {
            if (prior.local_name == local_name) {
                module_syntax_error(where, "duplicate imported binding '" + local_name + "'");
            }
        }
    }
    for (const auto& prior : dependency.bindings) {
        if (prior.local_name == local_name) {
            module_syntax_error(where, "duplicate imported binding '" + local_name + "'");
        }
    }
    ModuleImportBinding binding;
    binding.imported_name = std::move(imported_name);
    binding.local_name = std::move(local_name);
    binding.slot = "i" + std::to_string(slot_serial++);
    binding.namespace_import = namespace_import;
    dependency.bindings.push_back(std::move(binding));
}

std::size_t parse_module_import(const std::vector<JsToken>& tokens,
                                std::size_t begin,
                                ModuleTransform& transform,
                                std::size_t& slot_serial) {
    const JsToken& import_token = tokens[begin];
    std::size_t pos = begin + 1;
    if (pos >= tokens.size()) module_syntax_error(import_token, "incomplete import declaration");

    ModuleDependency dependency;
    if (tokens[pos].type == JsTokenType::String) {
        dependency.specifier = tokens[pos++].text;
    } else {
        auto add_default = [&] {
            const JsToken& local = tokens[pos];
            if (!module_binding_token(local)) {
                module_syntax_error(local, "expected an identifier after import");
            }
            module_add_import(transform, local, dependency, "default", module_token_name(local), false, slot_serial);
            ++pos;
        };
        auto add_namespace = [&] {
            const JsToken& star = tokens[pos++];
            (void)star;
            if (pos >= tokens.size() || !module_contextual(tokens[pos], "as")) {
                module_syntax_error(tokens[std::min(pos, tokens.size() - 1)], "expected 'as' after import *");
            }
            ++pos;
            if (pos >= tokens.size() || !module_binding_token(tokens[pos])) {
                module_syntax_error(tokens[std::min(pos, tokens.size() - 1)], "expected namespace binding after import * as");
            }
            const JsToken& local = tokens[pos++];
            module_add_import(transform, local, dependency, {}, module_token_name(local), true, slot_serial);
        };
        auto add_named = [&] {
            ++pos; // {
            while (pos < tokens.size() && tokens[pos].type != JsTokenType::CloseBrace) {
                const JsToken& imported = tokens[pos];
                if (!module_export_name_token(imported)) {
                    module_syntax_error(imported, "expected imported name inside import list");
                }
                const std::string imported_name = module_token_name(imported);
                ++pos;
                std::string local_name = imported_name;
                const JsToken* local_where = &imported;
                if (pos < tokens.size() && module_contextual(tokens[pos], "as")) {
                    ++pos;
                    if (pos >= tokens.size() || !module_binding_token(tokens[pos])) {
                        module_syntax_error(tokens[std::min(pos, tokens.size() - 1)], "expected local binding after as");
                    }
                    local_where = &tokens[pos];
                    local_name = module_token_name(tokens[pos++]);
                }
                if (!module_binding_token(*local_where)) {
                    module_syntax_error(*local_where, "an imported binding must be an identifier");
                }
                module_add_import(transform, *local_where, dependency, imported_name, local_name, false, slot_serial);
                if (pos < tokens.size() && tokens[pos].type == JsTokenType::Comma) {
                    ++pos;
                    continue;
                }
                if (pos >= tokens.size() || tokens[pos].type != JsTokenType::CloseBrace) {
                    module_syntax_error(tokens[std::min(pos, tokens.size() - 1)], "expected ',' or '}' in import list");
                }
            }
            if (pos >= tokens.size() || tokens[pos].type != JsTokenType::CloseBrace) {
                module_syntax_error(tokens.back(), "unterminated import list");
            }
            ++pos;
        };

        if (module_binding_token(tokens[pos])) {
            add_default();
            if (pos < tokens.size() && tokens[pos].type == JsTokenType::Comma) ++pos;
        }
        if (pos < tokens.size() && tokens[pos].type == JsTokenType::Star) {
            add_namespace();
        } else if (pos < tokens.size() && tokens[pos].type == JsTokenType::OpenBrace) {
            add_named();
        }
        if (pos >= tokens.size() || !module_contextual(tokens[pos], "from")) {
            module_syntax_error(tokens[std::min(pos, tokens.size() - 1)], "expected 'from' and a module specifier");
        }
        ++pos;
        if (pos >= tokens.size() || tokens[pos].type != JsTokenType::String) {
            module_syntax_error(tokens[std::min(pos, tokens.size() - 1)], "expected a string module specifier");
        }
        dependency.specifier = tokens[pos++].text;
    }

    const std::size_t end = module_statement_end(tokens, pos);
    transform.dependencies.push_back(std::move(dependency));
    return end;
}

void collect_simple_exported_declarations(const std::vector<JsToken>& tokens,
                                          std::size_t first,
                                          ModuleTransform& transform) {
    // The normal parser handles the declarations themselves. We only need the
    // names to copy into the export namespace after the wrapper has evaluated.
    // Binding patterns are deliberately refused here instead of silently
    // omitting one of their exported bindings.
    std::size_t pos = first;
    bool need_binding = true;
    int paren = 0;
    int bracket = 0;
    int brace = 0;
    while (pos < tokens.size()) {
        const JsToken& token = tokens[pos];
        if (paren == 0 && bracket == 0 && brace == 0) {
            if (token.type == JsTokenType::Semicolon || token.type == JsTokenType::Eof ||
                token.type == JsTokenType::Export || token.type == JsTokenType::Import) {
                return;
            }
            if (need_binding) {
                if (!module_binding_token(token)) {
                    module_syntax_error(token, "exported var/let/const declarations require simple identifier bindings");
                }
                module_add_export(transform, token, module_token_name(token), module_token_name(token));
                need_binding = false;
            }
            if (token.type == JsTokenType::Comma) {
                need_binding = true;
                ++pos;
                continue;
            }
        }
        switch (token.type) {
            case JsTokenType::OpenParen: ++paren; break;
            case JsTokenType::CloseParen: if (paren > 0) --paren; break;
            case JsTokenType::OpenBracket: ++bracket; break;
            case JsTokenType::CloseBracket: if (bracket > 0) --bracket; break;
            case JsTokenType::OpenBrace: ++brace; break;
            case JsTokenType::CloseBrace: if (brace > 0) --brace; break;
            default: break;
        }
        ++pos;
    }
}

std::size_t parse_module_export(const std::vector<JsToken>& tokens,
                                const std::string& source,
                                std::size_t begin,
                                ModuleTransform& transform,
                                std::vector<ModuleSourceEdit>& edits,
                                std::size_t& name_serial,
                                std::size_t& slot_serial) {
    const JsToken& export_token = tokens[begin];
    std::size_t pos = begin + 1;
    if (pos >= tokens.size()) module_syntax_error(export_token, "incomplete export declaration");

    if (tokens[pos].type == JsTokenType::Default) {
        const JsToken& default_token = tokens[pos++];
        const std::string local_name = module_fresh_identifier(source, name_serial, "default");
        module_add_export(transform, default_token, "default", local_name);
        edits.push_back({export_token.start, default_token.end, "const " + local_name + " ="});
        // Resume at the token after `export`, not after `default`: a default
        // object/class/function can begin with a delimiter that the outer
        // depth tracker must still see.
        return begin + 1;
    }

    if (tokens[pos].type == JsTokenType::Var || tokens[pos].type == JsTokenType::Let ||
        tokens[pos].type == JsTokenType::Const) {
        collect_simple_exported_declarations(tokens, pos + 1, transform);
        edits.push_back({export_token.start, export_token.end, {}});
        return begin + 1;
    }

    std::size_t name_pos = pos;
    if (module_contextual(tokens[pos], "async") && pos + 1 < tokens.size() &&
        tokens[pos + 1].type == JsTokenType::Function) {
        name_pos = pos + 2;
    } else if (tokens[pos].type == JsTokenType::Function) {
        name_pos = pos + 1;
    } else if (tokens[pos].type == JsTokenType::Class) {
        name_pos = pos + 1;
    }
    if (name_pos != pos) {
        if (name_pos < tokens.size() && tokens[name_pos].type == JsTokenType::Star) ++name_pos;
        if (name_pos >= tokens.size() || !module_binding_token(tokens[name_pos])) {
            module_syntax_error(tokens[std::min(name_pos, tokens.size() - 1)],
                                "exported function and class declarations require a name");
        }
        module_add_export(transform, tokens[name_pos], module_token_name(tokens[name_pos]), module_token_name(tokens[name_pos]));
        edits.push_back({export_token.start, export_token.end, {}});
        return begin + 1;
    }

    if (tokens[pos].type == JsTokenType::OpenBrace) {
        struct NamedExport {
            std::string local;
            std::string exported;
            const JsToken* local_where;
            const JsToken* export_where;
        };
        std::vector<NamedExport> names;
        ++pos;
        while (pos < tokens.size() && tokens[pos].type != JsTokenType::CloseBrace) {
            const JsToken& local = tokens[pos];
            if (!module_export_name_token(local)) {
                module_syntax_error(local, "expected local name inside export list");
            }
            const std::string local_name = module_token_name(local);
            ++pos;
            std::string exported_name = local_name;
            const JsToken* export_where = &local;
            if (pos < tokens.size() && module_contextual(tokens[pos], "as")) {
                ++pos;
                if (pos >= tokens.size() || !module_export_name_token(tokens[pos])) {
                    module_syntax_error(tokens[std::min(pos, tokens.size() - 1)], "expected exported name after as");
                }
                export_where = &tokens[pos];
                exported_name = module_token_name(tokens[pos++]);
            }
            names.push_back({local_name, exported_name, &local, export_where});
            if (pos < tokens.size() && tokens[pos].type == JsTokenType::Comma) {
                ++pos;
                continue;
            }
            if (pos >= tokens.size() || tokens[pos].type != JsTokenType::CloseBrace) {
                module_syntax_error(tokens[std::min(pos, tokens.size() - 1)], "expected ',' or '}' in export list");
            }
        }
        if (pos >= tokens.size() || tokens[pos].type != JsTokenType::CloseBrace) {
            module_syntax_error(tokens.back(), "unterminated export list");
        }
        ++pos;

        if (pos < tokens.size() && module_contextual(tokens[pos], "from")) {
            ++pos;
            if (pos >= tokens.size() || tokens[pos].type != JsTokenType::String) {
                module_syntax_error(tokens[std::min(pos, tokens.size() - 1)], "expected a string module specifier after from");
            }
            ModuleDependency dependency;
            dependency.specifier = tokens[pos++].text;
            for (const auto& name : names) {
                const std::string generated = module_fresh_identifier(source, name_serial, "reexport");
                module_add_import(transform, *name.export_where, dependency, name.local, generated, false, slot_serial);
                module_add_export(transform, *name.export_where, name.exported, generated);
            }
            transform.dependencies.push_back(std::move(dependency));
        } else {
            for (const auto& name : names) {
                if (!module_binding_token(*name.local_where)) {
                    module_syntax_error(*name.local_where, "local export bindings must be identifiers");
                }
                module_add_export(transform, *name.export_where, name.exported, name.local);
            }
        }

        const std::size_t end = module_statement_end(tokens, pos);
        const std::size_t end_byte = end > begin ? tokens[end - 1].end : export_token.end;
        edits.push_back({export_token.start, end_byte, {}});
        return end;
    }

    if (tokens[pos].type == JsTokenType::Star) {
        module_syntax_error(tokens[pos], "export * is not supported without live module bindings");
    }
    module_syntax_error(tokens[pos], "unsupported export form");
}

ModuleTransform transform_module_source(const std::string& source, std::size_t& name_serial) {
    JsLexer lexer(source);
    const std::vector<JsToken> tokens = lexer.tokenize_all();
    ModuleTransform transform;
    std::vector<ModuleSourceEdit> edits;
    std::size_t slot_serial = 0;
    int paren = 0;
    int bracket = 0;
    int brace = 0;

    for (std::size_t i = 0; i < tokens.size() && tokens[i].type != JsTokenType::Eof; ++i) {
        const JsToken& token = tokens[i];
        // `obj.import` / `obj.export` are ordinary keyword-named properties,
        // not module declarations. The classic parser accepts them after .
        const bool keyword_property = i > 0 &&
            (tokens[i - 1].type == JsTokenType::Dot || tokens[i - 1].type == JsTokenType::QuestionDot);
        if (paren == 0 && bracket == 0 && brace == 0 && !keyword_property && token.type == JsTokenType::Import &&
            (i + 1 >= tokens.size() || tokens[i + 1].type == JsTokenType::OpenParen ||
             tokens[i + 1].type == JsTokenType::Dot)) {
            module_syntax_error(token, "dynamic import() and import.meta are not supported");
        }
        if (paren == 0 && bracket == 0 && brace == 0 && !keyword_property && token.type == JsTokenType::Import) {
            const std::size_t end = parse_module_import(tokens, i, transform, slot_serial);
            const std::size_t end_byte = end > i ? tokens[end - 1].end : token.end;
            edits.push_back({token.start, end_byte, {}});
            i = end == 0 ? i : end - 1;
            continue;
        }
        if (paren == 0 && bracket == 0 && brace == 0 && !keyword_property && token.type == JsTokenType::Export) {
            const std::size_t next = parse_module_export(tokens, source, i, transform, edits, name_serial, slot_serial);
            i = next == 0 ? i : next - 1;
            continue;
        }
        switch (token.type) {
            case JsTokenType::OpenParen: ++paren; break;
            case JsTokenType::CloseParen: if (paren > 0) --paren; break;
            case JsTokenType::OpenBracket: ++bracket; break;
            case JsTokenType::CloseBracket: if (bracket > 0) --bracket; break;
            case JsTokenType::OpenBrace: ++brace; break;
            case JsTokenType::CloseBrace: if (brace > 0) --brace; break;
            default: break;
        }
    }

    std::sort(edits.begin(), edits.end(), [](const ModuleSourceEdit& a, const ModuleSourceEdit& b) {
        return a.begin < b.begin;
    });
    std::size_t cursor = 0;
    for (const auto& edit : edits) {
        if (edit.begin < cursor || edit.begin > source.size() || edit.end < edit.begin || edit.end > source.size()) {
            throw std::runtime_error("Invalid internal module-source rewrite range");
        }
        transform.body.append(source, cursor, edit.begin - cursor);
        transform.body += edit.replacement;
        cursor = edit.end;
    }
    transform.body.append(source, cursor, std::string::npos);
    return transform;
}

class WorkerModuleLoader {
public:
    WorkerModuleLoader(JsInterpreter& interpreter, ResourceRequestContext context)
        : m_interpreter(interpreter), m_context(std::move(context)) {}

    void evaluate_entry(const std::string& source, const std::string& resolved_url) {
        const std::string key = resolved_url.empty() ? std::string("<module-worker-entry>") : resolved_url;
        (void)evaluate_source(key, source);
    }

private:
    struct ModuleRecord {
        enum class State { Evaluating, Evaluated, Failed };
        State state{State::Evaluating};
        JsValue exports{JsValue::make_undefined()};
    };

    JsValue evaluate_dependency(const std::string& specifier, const std::string& referrer_url) {
        ResourceRequestContext context = m_context;
        context.base_url = referrer_url;
        context.mode = RequestMode::Cors;

        const std::string requested = ResourceLoader::resolve_url(context.base_url, specifier);
        if (auto alias = m_aliases.find(requested); alias != m_aliases.end()) {
            if (auto existing = m_records.find(alias->second); existing != m_records.end()) {
                return evaluate_existing(alias->second, existing->second);
            }
        }

        std::string error;
        std::string loaded_url;
        auto source = load_worker_script(specifier, context, RequestMode::Cors, error, loaded_url);
        if (!source) {
            js_throw_error(JsErrorKind::Error,
                           "NetworkError: Failed to load module '" + specifier + "': " + error);
        }
        if (loaded_url.empty()) loaded_url = requested.empty() ? specifier : requested;
        m_aliases[requested] = loaded_url;
        return evaluate_source(loaded_url, *source);
    }

    JsValue evaluate_existing(const std::string& key, const std::shared_ptr<ModuleRecord>& record) {
        if (record->state == ModuleRecord::State::Evaluated) return record->exports;
        if (record->state == ModuleRecord::State::Evaluating) {
            js_throw_error(JsErrorKind::Error,
                           "Unsupported ES module worker graph: cyclic dependency involving '" + key + "'.");
        }
        js_throw_error(JsErrorKind::Error, "Module '" + key + "' previously failed to evaluate.");
    }

    JsValue evaluate_source(const std::string& key, const std::string& source) {
        if (auto existing = m_records.find(key); existing != m_records.end()) {
            return evaluate_existing(key, existing->second);
        }
        if (m_records.size() >= kMaxModuleRecords) {
            js_throw_error(JsErrorKind::RangeError,
                           "Module worker dependency graph exceeds the 256-module limit.");
        }

        auto record = std::make_shared<ModuleRecord>();
        m_records.emplace(key, record);
        try {
            ModuleTransform module = transform_module_source(source, m_name_serial);
            JsValue imports = JsValue::make_object(Heap::default_heap().allocate_object());
            for (const ModuleDependency& dependency : module.dependencies) {
                const JsValue dependency_exports = evaluate_dependency(dependency.specifier, key);
                for (const ModuleImportBinding& binding : dependency.bindings) {
                    if (binding.namespace_import) {
                        imports.set_property(binding.slot, dependency_exports);
                    } else {
                        if (!dependency_exports.has_own_property(binding.imported_name)) {
                            js_throw_error(JsErrorKind::SyntaxError,
                                           "Module '" + dependency.specifier + "' does not export '" +
                                               binding.imported_name + "'.");
                        }
                        imports.set_property(binding.slot, dependency_exports.get_property(binding.imported_name));
                    }
                }
            }

            JsValue exports = JsValue::make_object(Heap::default_heap().allocate_object());
            const std::string imports_name = module_fresh_identifier(source, m_name_serial, "imports");
            const std::string exports_name = module_fresh_identifier(source, m_name_serial, "exports");
            m_interpreter.global_env()->define(imports_name, imports);
            m_interpreter.global_env()->define(exports_name, exports);

            std::string wrapper;
            wrapper.reserve(module.body.size() + module.exports.size() * 56 + 256);
            wrapper += "(function(" + imports_name + ", " + exports_name + "){\n\"use strict\";\n";
            for (const ModuleDependency& dependency : module.dependencies) {
                for (const ModuleImportBinding& binding : dependency.bindings) {
                    wrapper += "const " + binding.local_name + " = " + imports_name + "[" +
                               module_quote(binding.slot) + "];\n";
                }
            }
            wrapper += module.body;
            wrapper += "\n";
            for (const ModuleExportBinding& binding : module.exports) {
                wrapper += exports_name + "[" + module_quote(binding.exported_name) + "] = " +
                           binding.local_name + ";\n";
            }
            wrapper += "})(" + imports_name + ", " + exports_name + ");";
            m_interpreter.run(wrapper);

            record->exports = exports;
            record->state = ModuleRecord::State::Evaluated;
            return exports;
        } catch (...) {
            record->state = ModuleRecord::State::Failed;
            throw;
        }
    }

    static constexpr std::size_t kMaxModuleRecords = 256;
    JsInterpreter& m_interpreter;
    ResourceRequestContext m_context;
    std::unordered_map<std::string, std::shared_ptr<ModuleRecord>> m_records;
    std::unordered_map<std::string, std::string> m_aliases;
    std::size_t m_name_serial{0};
};

WorkerOptions parse_worker_options(const std::vector<JsValue>& args) {
    WorkerOptions options;
    if (args.size() < 2 || args[1].is_nullish()) {
        return options;
    }
    if (!args[1].is_object_like()) {
        js_throw_type_error("Failed to construct 'Worker': parameter 2 is not of type 'WorkerOptions'");
    }
    JsValue type_val = args[1].get_property("type");
    if (!type_val.is_undefined()) {
        const std::string type_str = type_val.to_string();
        if (type_str == "classic") {
            options.type = WorkerType::Classic;
        } else if (type_str == "module") {
            options.type = WorkerType::Module;
        } else {
            js_throw_type_error("Failed to construct 'Worker': The provided value '" + type_str +
                                "' is not a valid enum value of type WorkerType");
        }
    }
    JsValue name_val = args[1].get_property("name");
    if (!name_val.is_undefined()) {
        options.name = name_val.to_string();
    }
    return options;
}

[[noreturn]] void throw_data_clone_error(std::string_view receiver, const std::string& reason) {
    dom::throw_dom(dom::current_realm(), "DataCloneError",
                   "Failed to execute 'postMessage' on '" + std::string(receiver) + "': " + reason);
    std::abort(); // throw_dom always throws; keeps the noreturn contract explicit.
}

void reject_nonempty_transfer_list(const std::vector<JsValue>& args, std::size_t index,
                                   std::string_view receiver) {
    if (args.size() <= index || args[index].is_undefined()) return;
    // The Web IDL argument is a sequence. Asking the iteration protocol also
    // makes a non-iterable value fail as TypeError rather than being silently
    // accepted. No transferable type exists in this engine yet, but an empty
    // list remains a valid no-op for compatible callers.
    if (!js_iterable_to_vector(args[index]).empty()) {
        throw_data_clone_error(receiver, "transfer lists are not supported.");
    }
}

std::shared_ptr<JsStructuredClone> clone_message_or_throw(const JsValue& value,
                                                           std::string_view receiver) {
    auto packet = std::make_shared<JsStructuredClone>();
    std::string error;
    if (!js_write_structured_clone(value, *packet, error)) {
        throw_data_clone_error(receiver, error.empty() ? "The value could not be cloned." : error);
    }
    return packet;
}

JsValue make_worker_location(const std::string& resolved_url) {
    JsValue loc = JsValue::make_object(Heap::default_heap().allocate_object());
    Url parsed;
    if (!resolved_url.empty() && Url::parse(resolved_url, nullptr, parsed)) {
        loc.set_property("href", JsValue::make_string(parsed.href()));
        loc.set_property("origin", JsValue::make_string(parsed.origin()));
        loc.set_property("protocol", JsValue::make_string(parsed.protocol()));
        loc.set_property("host", JsValue::make_string(parsed.host_and_port()));
        loc.set_property("hostname", JsValue::make_string(parsed.hostname()));
        loc.set_property("port", JsValue::make_string(parsed.port_text()));
        loc.set_property("pathname", JsValue::make_string(parsed.pathname()));
        loc.set_property("search", JsValue::make_string(parsed.search()));
        loc.set_property("hash", JsValue::make_string(parsed.hash()));
    } else {
        loc.set_property("href", JsValue::make_string(resolved_url));
        loc.set_property("origin", JsValue::make_string("null"));
        loc.set_property("protocol", JsValue::make_string(""));
        loc.set_property("host", JsValue::make_string(""));
        loc.set_property("hostname", JsValue::make_string(""));
        loc.set_property("port", JsValue::make_string(""));
        loc.set_property("pathname", JsValue::make_string(""));
        loc.set_property("search", JsValue::make_string(""));
        loc.set_property("hash", JsValue::make_string(""));
    }
    std::string href_copy = loc.get_property("href").to_string();
    loc.set_property("toString", JsValue::make_native_function([href_copy](const std::vector<JsValue>&) {
        return JsValue::make_string(href_copy);
    }));
    return loc;
}

// ---------------------------------------------------------------------------
// Process-wide worker lifecycle registry & safe exit coordination
// ---------------------------------------------------------------------------

std::atomic<bool> g_process_shutting_down{false};

// Serializes JsInterpreter startup on worker threads so concurrent `new Worker`
// calls never race on shared static/tracer initialization (e.g. Heap's
// environment tracer or symbol registry setup).
std::mutex& worker_init_mutex() {
    static auto* m = new std::mutex();
    return *m;
}

struct WorkerRegistry {
    std::mutex mutex;
    std::vector<std::weak_ptr<WorkerObject>> workers;

    static WorkerRegistry& instance() {
        // Intentionally never destroyed so shutdown/cleanup never races with
        // static destruction order.
        static auto* reg = new WorkerRegistry();
        return *reg;
    }

    void add(const std::shared_ptr<WorkerObject>& worker) {
        std::lock_guard<std::mutex> lock(mutex);
        // Prune expired entries opportunistically.
        workers.erase(
            std::remove_if(workers.begin(), workers.end(),
                           [](const std::weak_ptr<WorkerObject>& w) { return w.expired(); }),
            workers.end());
        workers.push_back(worker);
    }

    std::vector<std::shared_ptr<WorkerObject>> take_live() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::shared_ptr<WorkerObject>> live;
        live.reserve(workers.size());
        for (const auto& weak : workers) {
            if (auto w = weak.lock()) live.push_back(std::move(w));
        }
        workers.clear();
        return live;
    }
};

// Shared lifetime state for workers owned by the main page thread.
std::shared_ptr<WorkerOwnerLoopState> main_owner_state() {
    static auto* holder = new std::shared_ptr<WorkerOwnerLoopState>([] {
        auto state = std::make_shared<WorkerOwnerLoopState>();
        state->loop = &EventLoop::default_loop();
        state->alive = true;
        return state;
    }());
    return *holder;
}

thread_local WorkerObject* t_current_worker = nullptr;
thread_local std::shared_ptr<WorkerOwnerLoopState>* t_current_worker_loop_state = nullptr;

void atexit_shutdown_workers() {
    g_process_shutting_down.store(true, std::memory_order_release);
    auto main_state = main_owner_state();
    {
        std::lock_guard<std::mutex> lock(main_state->mutex);
        main_state->alive = false;
        main_state->loop = nullptr;
    }
    WorkerObject::shutdown_all_workers();
}

void ensure_atexit_registered() {
    static std::once_flag once;
    std::call_once(once, [] {
        // Warm up static tables used by worker threads (JsLexer's keyword
        // table, JsOps's symbol_registry, and DomBindings's interface table)
        // BEFORE registering std::atexit so LIFO teardown guarantees
        // atexit_shutdown_workers() joins every worker thread before any
        // static table destructor runs.
        JsLexer warmup("var");
        (void)warmup.next_token();
        (void)js_symbol_for_key("Symbol.iterator", "Symbol.iterator");
        (void)dom::find_interface("Event");
        std::atexit(&atexit_shutdown_workers);
    });
}

} // namespace

struct WorkerOwnerJsState {
    JsValue js_instance{JsValue::make_undefined()};
    JsValue onmessage{JsValue::make_undefined()};
    JsValue onerror{JsValue::make_undefined()};
    ListenerMap listeners;

    void clear() {
        js_instance = JsValue::make_undefined();
        onmessage = JsValue::make_undefined();
        onerror = JsValue::make_undefined();
        listeners.clear();
    }
};

bool WorkerObject::is_javascript_mime_type(std::string_view content_type) {
    static constexpr std::array<std::string_view, 16> kJsMimeTypes = {
        "application/ecmascript",
        "application/javascript",
        "application/x-ecmascript",
        "application/x-javascript",
        "text/ecmascript",
        "text/javascript",
        "text/javascript1.0",
        "text/javascript1.1",
        "text/javascript1.2",
        "text/javascript1.3",
        "text/javascript1.4",
        "text/javascript1.5",
        "text/jscript",
        "text/livescript",
        "text/x-ecmascript",
        "text/x-javascript",
    };
    const std::string essence = extract_mime_essence(content_type);
    for (std::string_view allowed : kJsMimeTypes) {
        if (essence == allowed) return true;
    }
    return false;
}

void WorkerObject::shutdown_all_workers() {
    // Loop because a worker finishing teardown may have registered a nested
    // worker just before g_process_shutting_down was observed.
    while (true) {
        auto live = WorkerRegistry::instance().take_live();
        if (live.empty()) break;
        // Wake all workers first so they exit in parallel rather than serially.
        for (const auto& worker : live) {
            worker->m_terminated.store(true, std::memory_order_release);
            worker->m_running.store(false, std::memory_order_release);
            worker->m_cv.notify_all();
        }
        for (const auto& worker : live) {
            worker->terminate();
        }
    }
}

WorkerObject::WorkerObject(std::string script_url,
                           ResourceRequestContext context,
                           WorkerOptions options)
    : m_script_url(std::move(script_url)),
      m_resolved_url(m_script_url),
      m_context(std::move(context)),
      m_options(std::move(options)),
      m_owner_thread_id(std::this_thread::get_id()),
      m_owner_js(std::make_shared<WorkerOwnerJsState>()) {
    if (t_current_worker_loop_state && *t_current_worker_loop_state) {
        m_owner_state = *t_current_worker_loop_state;
    } else {
        m_owner_state = main_owner_state();
    }
}

WorkerObject::~WorkerObject() {
    terminate();
    if (m_owner_js) {
        if (std::this_thread::get_id() == m_owner_thread_id) {
            m_owner_js.reset();
        } else {
            // Never decrement non-atomic owner-thread JsObject handle counts
            // from the worker thread; hand off destruction to the owner loop.
            auto orphan = std::move(m_owner_js);
            bool posted = false;
            if (m_owner_state) {
                std::lock_guard<std::mutex> lock(m_owner_state->mutex);
                if (m_owner_state->alive && m_owner_state->loop) {
                    m_owner_state->loop->post_task([orphan]() mutable { orphan.reset(); });
                    posted = true;
                }
            }
            if (!posted) {
                // Owner loop is already tearing down; keep the handle alive
                // alongside Heap::default_heap() instead of racing.
                new std::shared_ptr<WorkerOwnerJsState>(std::move(orphan));
            }
        }
    }
}

void WorkerObject::set_onmessage(JsValue handler) {
    if (m_owner_js) m_owner_js->onmessage = std::move(handler);
}

JsValue WorkerObject::onmessage() const {
    return m_owner_js ? m_owner_js->onmessage : JsValue::make_undefined();
}

void WorkerObject::set_onerror(JsValue handler) {
    if (m_owner_js) m_owner_js->onerror = std::move(handler);
}

JsValue WorkerObject::onerror() const {
    return m_owner_js ? m_owner_js->onerror : JsValue::make_undefined();
}

void WorkerObject::add_event_listener(const std::string& event, JsValue listener, bool once) {
    if (m_owner_js) m_owner_js->listeners.add(event, listener, once);
}

void WorkerObject::remove_event_listener(const std::string& event, const JsValue& listener) {
    if (m_owner_js) m_owner_js->listeners.remove(event, listener);
}

void WorkerObject::release_owner_js() {
    if (std::this_thread::get_id() == m_owner_thread_id && m_owner_js) {
        m_owner_js->clear();
    }
}

void WorkerObject::register_child(const std::shared_ptr<WorkerObject>& child) {
    std::lock_guard<std::mutex> lock(m_children_mutex);
    m_children.push_back(child);
}

void WorkerObject::terminate_children() {
    std::vector<std::shared_ptr<WorkerObject>> live;
    {
        std::lock_guard<std::mutex> lock(m_children_mutex);
        for (const auto& weak : m_children) {
            if (auto c = weak.lock()) live.push_back(std::move(c));
        }
        m_children.clear();
    }
    for (const auto& child : live) {
        child->m_terminated.store(true, std::memory_order_release);
        child->m_running.store(false, std::memory_order_release);
        child->m_cv.notify_all();
    }
    for (const auto& child : live) {
        child->terminate();
    }
}

void WorkerObject::post_to_owner(std::function<void()> task) {
    if (!task || !m_owner_state) return;
    std::function<void()> wake;
    {
        std::lock_guard<std::mutex> lock(m_owner_state->mutex);
        if (!m_owner_state->alive || !m_owner_state->loop) return;
        m_owner_state->loop->post_task(std::move(task));
        wake = m_owner_state->wake;
    }
    if (wake) wake();
}

void WorkerObject::start() {
    if (g_process_shutting_down.load(std::memory_order_acquire) ||
        m_terminated.load(std::memory_order_acquire)) {
        m_running.store(false, std::memory_order_release);
        return;
    }

    // Classic workers keep their historical same-origin top-level fetch. A
    // module worker follows the module-script CORS path; dependency fetches
    // use the same mode in WorkerModuleLoader below.
    const RequestMode top_level_mode = m_options.type == WorkerType::Module
                                           ? RequestMode::Cors
                                           : RequestMode::SameOrigin;
    std::string load_error;
    std::string loaded_url;
    auto script_source = load_worker_script(m_script_url, m_context, top_level_mode,
                                            load_error, loaded_url);
    if (!loaded_url.empty()) m_resolved_url = loaded_url;

    if (!script_source) {
        m_running.store(false, std::memory_order_release);
        m_terminated.store(false, std::memory_order_release);
        post_error(load_error, m_resolved_url, 0, 0, /*release_after=*/true);
        return;
    }

    // A classic worker's relative references resolve against its script URL.
    // The capability still comes from the creator's document, not from a
    // <base> element the script URL happened to contain.
    if (!loaded_url.empty() && loaded_url.rfind("data:", 0) != 0) {
        m_context.base_url = loaded_url;
    }

    ensure_atexit_registered();
    auto self = shared_from_this();
    WorkerRegistry::instance().add(self);
    if (t_current_worker) {
        t_current_worker->register_child(self);
    }

    m_running.store(true, std::memory_order_release);
    m_terminated.store(false, std::memory_order_release);

    std::lock_guard<std::mutex> lock(m_thread_mutex);
    if (g_process_shutting_down.load(std::memory_order_acquire) ||
        m_terminated.load(std::memory_order_acquire)) {
        m_running.store(false, std::memory_order_release);
        return;
    }
    m_thread = std::thread([self = std::move(self), script = std::move(*script_source)]() mutable {
        self->worker_thread_main(std::move(script));
    });
}

void WorkerObject::dispatch_page_error(const std::string& message,
                                       const std::string& filename,
                                       int lineno,
                                       int colno) {
    if (!m_owner_js || m_terminated.load(std::memory_order_acquire)) return;

    JsValue inst = m_owner_js->js_instance;
    JsValue target = inst.is_object_like() ? inst : JsValue::make_null();
    auto control = std::make_shared<EventControl>();

    JsValue err_event = JsValue::make_object(Heap::default_heap().allocate_object());
    err_event.set_property("type", JsValue::make_string("error"));
    err_event.set_property("message", JsValue::make_string(message));
    err_event.set_property("filename", JsValue::make_string(filename.empty() ? m_resolved_url : filename));
    err_event.set_property("lineno", JsValue::make_number(lineno));
    err_event.set_property("colno", JsValue::make_number(colno));
    err_event.set_property("error", JsValue::make_null());
    err_event.set_property("target", target);
    err_event.set_property("currentTarget", target);
    attach_event_methods(err_event, control, /*cancelable=*/true);

    bool had_handler = false;

    auto invoke_onerror_handler = [&](const JsValue& handler) {
        if (!handler.is_function()) return;
        had_handler = true;
        JsValue ret = JsValue::make_undefined();
        JsValue thrown;
        if (js_capture_throw([&]() { ret = handler.call_with_this(target, {err_event}); }, thrown)) {
            if (ret.type == JsType::Boolean && ret.bool_val) {
                control->default_prevented = true;
                err_event.set_property("defaultPrevented", JsValue::make_boolean(true));
            }
        }
    };

    JsValue prop_handler = inst.is_object_like() ? inst.get_property("onerror") : JsValue::make_undefined();
    if (prop_handler.is_function()) {
        invoke_onerror_handler(prop_handler);
    }
    if (!control->immediate_stopped && m_owner_js && m_owner_js->onerror.is_function() &&
        (!prop_handler.is_function() || !strict_equals(m_owner_js->onerror, prop_handler))) {
        invoke_onerror_handler(m_owner_js->onerror);
    }

    auto listeners = m_owner_js ? m_owner_js->listeners.snapshot("error") : std::vector<EventListenerEntry>{};
    for (const auto& entry : listeners) {
        if (control->immediate_stopped) break;
        if (entry.once && m_owner_js) {
            m_owner_js->listeners.remove("error", entry.callback);
        }
        had_handler = true;
        invoke_event_listener(entry.callback, target, err_event, nullptr);
    }

    if (!had_handler && !control->default_prevented) {
        std::cerr << "Uncaught (in worker) " << message << std::endl;
    }
}

void WorkerObject::dispatch_page_message(const std::shared_ptr<JsStructuredClone>& message) {
    if (!m_owner_js || m_terminated.load(std::memory_order_acquire)) return;

    // Materialize only on the owner agent. The packet itself contains no
    // JsValues, so a worker never races the page heap by retaining a source
    // object in an asynchronous callback.
    const JsValue data = message ? js_read_structured_clone(*message) : JsValue::make_undefined();

    JsValue inst = m_owner_js->js_instance;
    JsValue target = inst.is_object_like() ? inst : JsValue::make_null();
    auto control = std::make_shared<EventControl>();

    std::string origin;
    Url parsed;
    if (!m_resolved_url.empty() && Url::parse(m_resolved_url, nullptr, parsed)) {
        origin = parsed.origin();
    }

    JsValue event = JsValue::make_object(Heap::default_heap().allocate_object());
    event.set_property("type", JsValue::make_string("message"));
    event.set_property("data", data);
    event.set_property("origin", JsValue::make_string(origin));
    event.set_property("lastEventId", JsValue::make_string(""));
    event.set_property("source", JsValue::make_null());
    event.set_property("ports", JsValue::make_array({}));
    event.set_property("target", target);
    event.set_property("currentTarget", target);
    attach_event_methods(event, control, /*cancelable=*/false);

    auto call_msg_handler = [&](const JsValue& handler) {
        if (!handler.is_function()) return;
        JsValue thrown;
        if (!js_capture_throw([&]() { handler.call_with_this(target, {event}); }, thrown)) {
            dispatch_page_error(js_describe_thrown(thrown), m_resolved_url, 0, 0);
        }
    };

    JsValue prop_handler = inst.is_object_like() ? inst.get_property("onmessage") : JsValue::make_undefined();
    if (prop_handler.is_function()) {
        call_msg_handler(prop_handler);
    }
    if (!control->immediate_stopped && m_owner_js && m_owner_js->onmessage.is_function() &&
        (!prop_handler.is_function() || !strict_equals(m_owner_js->onmessage, prop_handler))) {
        call_msg_handler(m_owner_js->onmessage);
    }

    auto listeners = m_owner_js ? m_owner_js->listeners.snapshot("message") : std::vector<EventListenerEntry>{};
    for (const auto& entry : listeners) {
        if (control->immediate_stopped) break;
        if (entry.once && m_owner_js) {
            m_owner_js->listeners.remove("message", entry.callback);
        }
        JsValue thrown;
        invoke_event_listener(entry.callback, target, event, &thrown);
        if (!thrown.is_undefined()) {
            dispatch_page_error(js_describe_thrown(thrown), m_resolved_url, 0, 0);
        }
    }
}

void WorkerObject::dispatch_page_custom_event(const std::string& type, const JsValue& event) {
    if (!m_owner_js || m_terminated.load(std::memory_order_acquire)) return;
    JsValue inst = m_owner_js->js_instance;
    JsValue target = inst.is_object_like() ? inst : JsValue::make_null();

    auto listeners = m_owner_js->listeners.snapshot(type);
    for (const auto& entry : listeners) {
        if (entry.once && m_owner_js) {
            m_owner_js->listeners.remove(type, entry.callback);
        }
        invoke_event_listener(entry.callback, target, event, nullptr);
    }
}

void WorkerObject::post_error(const std::string& message,
                              const std::string& filename,
                              int lineno,
                              int colno,
                              bool release_after) {
    post_to_owner([weak_self = weak_from_this(), message, filename, lineno, colno, release_after]() {
        auto worker = weak_self.lock();
        if (!worker || worker->m_terminated.load(std::memory_order_acquire)) return;
        worker->dispatch_page_error(message, filename, lineno, colno);
        if (release_after && !worker->m_running.load(std::memory_order_acquire)) {
            worker->release_owner_js();
        }
    });
}

void WorkerObject::terminate() {
    m_terminated.store(true, std::memory_order_release);
    m_running.store(false, std::memory_order_release);
    m_cv.notify_all();

    std::thread to_join;
    {
        std::lock_guard<std::mutex> lock(m_thread_mutex);
        if (m_thread.joinable()) {
            if (std::this_thread::get_id() == m_thread.get_id()) {
                m_thread.detach();
            } else {
                to_join = std::move(m_thread);
            }
        }
    }
    if (to_join.joinable()) {
        to_join.join();
    }

    // Release owner-thread JS references immediately if on the owner thread,
    // breaking any inst <-> WorkerObject cycle.
    release_owner_js();
}

void WorkerObject::post_message(std::shared_ptr<JsStructuredClone> message) {
    if (m_terminated.load(std::memory_order_acquire) ||
        !m_running.load(std::memory_order_acquire)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_incoming_messages.push(std::move(message));
    }
    m_cv.notify_one();
}

void WorkerObject::worker_thread_main(std::string script_source) {
    if (m_terminated.load(std::memory_order_acquire) ||
        g_process_shutting_down.load(std::memory_order_acquire)) {
        m_running.store(false, std::memory_order_release);
        return;
    }

    // A worker is an agent of its own: its timers and promise reactions run on
    // this thread, in this loop, never in the page's. Declared before the
    // interpreter so that it outlives it.
    Heap::enter_new_agent();
    EventLoop worker_loop;
    EventLoop::set_current(&worker_loop);

    auto my_loop_state = std::make_shared<WorkerOwnerLoopState>();
    my_loop_state->loop = &worker_loop;
    my_loop_state->wake = [weak_self = weak_from_this()]() {
        auto owner = weak_self.lock();
        if (!owner) return;
        {
            std::lock_guard<std::mutex> lock(owner->m_queue_mutex);
            owner->m_owner_task_pending = true;
        }
        owner->m_cv.notify_one();
    };
    my_loop_state->alive = true;

    auto ws_event_sink = std::make_shared<WebSocketEventSink>();
    ws_event_sink->loop = &worker_loop;
    ws_event_sink->wake = my_loop_state->wake;
    ws_event_sink->alive = true;

    WorkerObject* prev_worker = t_current_worker;
    auto* prev_loop_state = t_current_worker_loop_state;
    t_current_worker = this;
    t_current_worker_loop_state = &my_loop_state;

    struct ThreadScopeGuard {
        WorkerObject* self;
        std::shared_ptr<WorkerOwnerLoopState> loop_state;
        std::shared_ptr<WebSocketEventSink> ws_sink;
        WorkerObject* prev_w;
        std::shared_ptr<WorkerOwnerLoopState>* prev_ls;
        ~ThreadScopeGuard() {
            if (ws_sink) {
                ws_sink->shutdown();
            }
            // Stop nested workers BEFORE worker_loop is destroyed so none can
            // post to a dead parent EventLoop.
            {
                std::lock_guard<std::mutex> lock(loop_state->mutex);
                loop_state->alive = false;
                loop_state->loop = nullptr;
                loop_state->wake = nullptr;
            }
            self->terminate_children();
            t_current_worker = prev_w;
            t_current_worker_loop_state = prev_ls;
            EventLoop::set_current(nullptr);
            self->m_running.store(false, std::memory_order_release);
            // Release owner-thread JS wrapper after all previously posted
            // messages/errors have been delivered in FIFO order.
            self->post_to_owner([weak_self = self->weak_from_this()]() {
                if (auto w = weak_self.lock()) {
                    if (!w->m_running.load(std::memory_order_acquire)) {
                        w->release_owner_js();
                    }
                }
            });
        }
    } scope_guard{this, my_loop_state, ws_event_sink, prev_worker, prev_loop_state};

    std::optional<JsInterpreter> worker_js_opt;
    {
        std::lock_guard<std::mutex> init_lock(worker_init_mutex());
        if (m_terminated.load(std::memory_order_acquire) ||
            g_process_shutting_down.load(std::memory_order_acquire)) {
            return;
        }
        worker_js_opt.emplace(nullptr);
    }
    JsInterpreter& worker_js = *worker_js_opt;
    auto env = worker_js.global_env();

    // The worker inherits the creator's resource capability. Its own fetch,
    // importScripts, and nested Worker constructors must not fall back to
    // the interpreter's default about:blank context.
    ResourceRequestContext worker_context = m_context;
    JsValue fetch_fn = JsValue::make_native_function(
        [worker_context](const std::vector<JsValue>& args) {
            return FetchAPI::fetch(args, worker_context);
        });
    JsValue nested_worker_ctor = WorkerObject::create_constructor(
        [worker_context]() { return worker_context; });

    env->define("fetch", fetch_fn);
    env->define("Worker", nested_worker_ctor);

    WebSocketContext ws_context;
    const bool is_opaque_worker =
        m_resolved_url.rfind("data:", 0) == 0 || m_resolved_url.rfind("blob:", 0) == 0;
    if (is_opaque_worker) {
        ws_context.opaque_origin = true;
        ws_context.document_url = (m_context.document_url.rfind("https:", 0) == 0)
            ? m_context.document_url
            : m_resolved_url;
        ws_context.base_url = m_resolved_url;
    } else {
        ws_context.opaque_origin = false;
        ws_context.document_url = m_resolved_url;
        ws_context.base_url = m_resolved_url;
    }
    JsValue ws_ctor = WebSocketObject::create_constructor(
        [ws_context]() { return ws_context; }, ws_event_sink);
    env->define("WebSocket", ws_ctor);

    ListenerMap worker_listeners;

    // self reference (DedicatedWorkerGlobalScope)
    JsValue self_obj = JsValue::make_object(Heap::default_heap().allocate_object());
    self_obj.set_property("self", self_obj);
    self_obj.set_property("name", JsValue::make_string(m_options.name));
    JsValue location_obj = make_worker_location(m_resolved_url);
    self_obj.set_property("location", location_obj);
    self_obj.set_property("fetch", fetch_fn);
    self_obj.set_property("Worker", nested_worker_ctor);
    self_obj.set_property("WebSocket", ws_ctor);
    self_obj.set_property("onmessage", JsValue::make_null());
    self_obj.set_property("onerror", JsValue::make_null());

    env->define("self", self_obj);
    env->define("name", JsValue::make_string(m_options.name));
    env->define("location", location_obj);

    // Uncaught errors inside the worker first notify self.onerror /
    // self.addEventListener("error"); if not canceled, forward to the page.
    auto report_error = [&](const std::string& message) {
        auto control = std::make_shared<EventControl>();
        JsValue err_event = JsValue::make_object(Heap::default_heap().allocate_object());
        err_event.set_property("type", JsValue::make_string("error"));
        err_event.set_property("message", JsValue::make_string(message));
        err_event.set_property("filename", JsValue::make_string(m_resolved_url));
        err_event.set_property("lineno", JsValue::make_number(0));
        err_event.set_property("colno", JsValue::make_number(0));
        err_event.set_property("error", JsValue::make_null());
        err_event.set_property("target", self_obj);
        err_event.set_property("currentTarget", self_obj);
        attach_event_methods(err_event, control, /*cancelable=*/true);

        JsValue global_onerr = env->get("onerror");
        JsValue self_onerr = self_obj.get_property("onerror");
        auto call_worker_onerror = [&](const JsValue& handler) {
            if (!handler.is_function()) return;
            JsValue ret = JsValue::make_undefined();
            JsValue ignored;
            if (js_capture_throw([&]() {
                    ret = handler.call_with_this(self_obj, {
                        JsValue::make_string(message),
                        JsValue::make_string(m_resolved_url),
                        JsValue::make_number(0),
                        JsValue::make_number(0),
                        JsValue::make_null()
                    });
                }, ignored)) {
                if (ret.type == JsType::Boolean && ret.bool_val) {
                    control->default_prevented = true;
                    err_event.set_property("defaultPrevented", JsValue::make_boolean(true));
                }
            }
        };
        if (global_onerr.is_function()) {
            call_worker_onerror(global_onerr);
        } else if (self_onerr.is_function()) {
            call_worker_onerror(self_onerr);
        }

        auto err_listeners = worker_listeners.snapshot("error");
        for (const auto& entry : err_listeners) {
            if (control->immediate_stopped) break;
            if (entry.once) worker_listeners.remove("error", entry.callback);
            invoke_event_listener(entry.callback, self_obj, err_event, nullptr);
        }

        if (!control->default_prevented) {
            post_error(message, m_resolved_url, 0, 0, /*release_after=*/false);
        }
    };

    auto run_script = [&report_error](const std::function<void()>& body) {
        JsValue thrown;
        bool ok = false;
        {
            JsScriptScope script_scope; // promise reactions run when the script is done
            ok = js_capture_throw(body, thrown);
        }
        if (!ok) report_error(js_describe_thrown(thrown));
        return ok;
    };

    // postMessage(data, transfer) inside a worker. Encode synchronously while
    // this agent owns `data`; the owner page later decodes an inert packet.
    auto post_msg_fn = JsValue::make_native_function([this](const std::vector<JsValue>& args) {
        if (args.empty() || m_terminated.load(std::memory_order_acquire)) {
            return JsValue::make_undefined();
        }
        reject_nonempty_transfer_list(args, 1, "WorkerGlobalScope");
        auto message = clone_message_or_throw(args[0], "WorkerGlobalScope");

        post_to_owner([weak_self = weak_from_this(), message = std::move(message)]() {
            auto worker = weak_self.lock();
            if (!worker || worker->m_terminated.load(std::memory_order_acquire)) return;
            worker->dispatch_page_message(message);
        });

        return JsValue::make_undefined();
    });
    env->define("postMessage", post_msg_fn);
    self_obj.set_property("postMessage", post_msg_fn);

    // close() inside worker
    auto close_fn = JsValue::make_native_function([this](const std::vector<JsValue>&) {
        m_running.store(false, std::memory_order_release);
        m_cv.notify_all();
        return JsValue::make_undefined();
    });
    env->define("close", close_fn);
    self_obj.set_property("close", close_fn);

    // addEventListener / removeEventListener inside worker
    auto add_el_fn = JsValue::make_native_function([&worker_listeners](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            const bool once = (args.size() >= 3) && read_once_option(args[2]);
            worker_listeners.add(args[0].to_string(), args[1], once);
        }
        return JsValue::make_undefined();
    });
    auto rem_el_fn = JsValue::make_native_function([&worker_listeners](const std::vector<JsValue>& args) {
        if (args.size() >= 2) {
            worker_listeners.remove(args[0].to_string(), args[1]);
        }
        return JsValue::make_undefined();
    });
    env->define("addEventListener", add_el_fn);
    self_obj.set_property("addEventListener", add_el_fn);
    env->define("removeEventListener", rem_el_fn);
    self_obj.set_property("removeEventListener", rem_el_fn);

    // importScripts() belongs only to a classic worker global. Module workers
    // must use their static import graph, and deliberately do not expose this
    // classic-script escape hatch.
    if (m_options.type == WorkerType::Classic) {
        // HTML §10.2.5.4, subject to the local-file boundary, HTTP 2xx check,
        // and WHATWG JavaScript MIME policy.
        auto import_scripts_fn = JsValue::make_native_function(
            [this, &worker_js, worker_context](const std::vector<JsValue>& args) {
                for (const auto& arg : args) {
                    if (m_terminated.load(std::memory_order_acquire) ||
                        g_process_shutting_down.load(std::memory_order_acquire)) {
                        return JsValue::make_undefined();
                    }
                    const std::string url = arg.to_string();
                    std::string err;
                    std::string loaded_url;
                    auto imported = load_worker_script(url, worker_context, RequestMode::NoCors,
                                                       err, loaded_url);
                    if (!imported) {
                        js_throw_error(JsErrorKind::Error,
                                       "NetworkError: Failed to execute 'importScripts' on 'WorkerGlobalScope': " + err);
                    }
                    worker_js.run(*imported);
                }
                return JsValue::make_undefined();
            });
        env->define("importScripts", import_scripts_fn);
        self_obj.set_property("importScripts", import_scripts_fn);
    }

    // Initial evaluation of the worker script. A worker whose script or
    // module graph fails to load/evaluate never enters the message loop.
    if (!run_script([&] {
            if (m_options.type == WorkerType::Module) {
                WorkerModuleLoader modules(worker_js, worker_context);
                modules.evaluate_entry(script_source, m_resolved_url);
            } else {
                worker_js.run(script_source);
            }
        })) {
        return;
    }
    worker_loop.tick();

    // Message pump: wait for a message from the page or for the worker's next
    // timer, whichever comes first.
    while (m_running.load(std::memory_order_acquire) &&
           !m_terminated.load(std::memory_order_acquire) &&
           !g_process_shutting_down.load(std::memory_order_acquire)) {
        std::shared_ptr<JsStructuredClone> incoming_message;
        bool have_message = false;
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            auto woken = [this]() {
                return m_owner_task_pending ||
                       !m_incoming_messages.empty() ||
                       !m_running.load(std::memory_order_acquire) ||
                       m_terminated.load(std::memory_order_acquire) ||
                       g_process_shutting_down.load(std::memory_order_acquire);
            };
            const auto next_timer = worker_loop.time_until_next();
            if (!next_timer) {
                m_cv.wait(lock, woken);
            } else if (next_timer->count() > 0) {
                m_cv.wait_for(lock, *next_timer, woken);
            }
            if (!m_running.load(std::memory_order_acquire) ||
                m_terminated.load(std::memory_order_acquire) ||
                g_process_shutting_down.load(std::memory_order_acquire)) {
                break;
            }
            m_owner_task_pending = false;
            if (!m_incoming_messages.empty()) {
                incoming_message = std::move(m_incoming_messages.front());
                m_incoming_messages.pop();
                have_message = true;
            }
        }

        if (have_message) {
            // Decode after the queue lock is released, in the worker's agent.
            const JsValue data = incoming_message ? js_read_structured_clone(*incoming_message)
                                                  : JsValue::make_undefined();

            auto control = std::make_shared<EventControl>();
            JsValue event = JsValue::make_object(Heap::default_heap().allocate_object());
            event.set_property("data", data);
            event.set_property("type", JsValue::make_string("message"));
            event.set_property("target", self_obj);
            event.set_property("currentTarget", self_obj);
            attach_event_methods(event, control, /*cancelable=*/false);

            JsValue onmsg = env->get("onmessage");
            if (!onmsg.is_function()) {
                onmsg = self_obj.get_property("onmessage");
            }
            if (onmsg.is_function()) {
                run_script([&] { onmsg.call_with_this(self_obj, {event}); });
            }
            const auto listeners = worker_listeners.snapshot("message");
            for (const auto& entry : listeners) {
                if (control->immediate_stopped) break;
                if (entry.once) worker_listeners.remove("message", entry.callback);
                run_script([&] {
                    JsValue thrown;
                    invoke_event_listener(entry.callback, self_obj, event, &thrown);
                    if (!thrown.is_undefined()) js_throw(thrown);
                });
            }
        }

        worker_loop.tick();
    }
}

JsValue WorkerObject::attach_js_instance(const std::shared_ptr<WorkerObject>& worker) {
    JsValue inst = JsValue::make_object(Heap::default_heap().allocate_object());
    if (worker->m_owner_js) {
        worker->m_owner_js->js_instance = inst;
    }

    inst.set_property("postMessage", JsValue::make_native_function([worker](const std::vector<JsValue>& msg_args) {
        if (msg_args.empty()) return JsValue::make_undefined();
        reject_nonempty_transfer_list(msg_args, 1, "Worker");
        worker->post_message(clone_message_or_throw(msg_args[0], "Worker"));
        return JsValue::make_undefined();
    }));

    inst.set_property("terminate", JsValue::make_native_function([worker](const std::vector<JsValue>&) {
        worker->terminate();
        return JsValue::make_undefined();
    }));

    inst.set_property("addEventListener", JsValue::make_native_function([worker](const std::vector<JsValue>& el_args) {
        if (el_args.size() >= 2) {
            const bool once = (el_args.size() >= 3) && read_once_option(el_args[2]);
            worker->add_event_listener(el_args[0].to_string(), el_args[1], once);
        }
        return JsValue::make_undefined();
    }));

    inst.set_property("removeEventListener", JsValue::make_native_function([worker](const std::vector<JsValue>& el_args) {
        if (el_args.size() >= 2) {
            worker->remove_event_listener(el_args[0].to_string(), el_args[1]);
        }
        return JsValue::make_undefined();
    }));

    inst.set_property("dispatchEvent", JsValue::make_native_function([worker](const std::vector<JsValue>& ev_args) {
        if (ev_args.empty() || !ev_args[0].is_object_like()) {
            js_throw_type_error("Failed to execute 'dispatchEvent' on 'Worker': parameter 1 is not of type 'Event'");
        }
        const std::string type = ev_args[0].get_property("type").to_string();
        worker->dispatch_page_custom_event(type, ev_args[0]);
        return JsValue::make_boolean(!ev_args[0].get_property("defaultPrevented").is_truthy());
    }));

    inst.set_property("onmessage", JsValue::make_null());
    inst.set_property("onerror", JsValue::make_null());

    return inst;
}

JsValue WorkerObject::create_js_instance(std::string_view script_url,
                                         ResourceRequestContext context,
                                         WorkerOptions options) {
    auto worker = std::make_shared<WorkerObject>(std::string(script_url),
                                                 std::move(context),
                                                 std::move(options));
    JsValue inst = attach_js_instance(worker);
    worker->start();
    return inst;
}

JsValue WorkerObject::create_constructor(ContextProvider context_provider) {
    return JsValue::make_native_function([context_provider = std::move(context_provider)](const std::vector<JsValue>& args) {
        if (!JsValue::current_is_construct()) {
            js_throw_type_error("Failed to construct 'Worker': Please use the 'new' operator");
        }
        if (args.empty()) {
            js_throw_type_error("Failed to construct 'Worker': 1 argument required, but only 0 present");
        }
        const std::string script_url = args[0].to_string();
        WorkerOptions options = parse_worker_options(args);
        auto worker = std::make_shared<WorkerObject>(script_url, context_provider(), std::move(options));
        JsValue inst = attach_js_instance(worker);
        worker->start();
        return inst;
    });
}

} // namespace litebird
