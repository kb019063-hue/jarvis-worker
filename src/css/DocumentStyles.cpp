#include "litebird/css/DocumentStyles.hpp"
#include "litebird/css/CssParser.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/ProcessingInstruction.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/net/DataUrl.hpp"
#include "litebird/net/UrlParser.hpp"

#include <cctype>
#include <unordered_set>

namespace litebird {

namespace {

std::string ascii_lower_str(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string_view trim_ascii(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n' || s.front() == '\f')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n' || s.back() == '\f')) {
        s.remove_suffix(1);
    }
    return s;
}

bool is_css_type_attr(std::string_view raw_type) {
    raw_type = trim_ascii(raw_type);
    if (raw_type.empty()) return true;
    auto semi = raw_type.find(';');
    if (semi != std::string_view::npos) raw_type = trim_ascii(raw_type.substr(0, semi));
    return ascii_lower_str(raw_type) == "text/css";
}

bool is_stylesheet_rel(std::string_view rel) {
    bool has_stylesheet = false;
    bool has_alternate = false;
    std::size_t i = 0;
    while (i < rel.size()) {
        while (i < rel.size() && std::isspace(static_cast<unsigned char>(rel[i]))) ++i;
        std::size_t start = i;
        while (i < rel.size() && !std::isspace(static_cast<unsigned char>(rel[i]))) ++i;
        if (i > start) {
            std::string tok = ascii_lower_str(rel.substr(start, i - start));
            if (tok == "stylesheet") has_stylesheet = true;
            else if (tok == "alternate") has_alternate = true;
        }
    }
    return has_stylesheet && !has_alternate;
}

bool media_matches_or_needs_wrapper(std::string_view raw_media, std::string& wrapped_media_out) {
    wrapped_media_out.clear();
    raw_media = trim_ascii(raw_media);
    if (raw_media.empty()) return true;
    const std::string lower = ascii_lower_str(raw_media);
    if (lower == "all" || lower == "screen" || lower == "screen, all" || lower == "all, screen") {
        return true;
    }
    if (lower == "print" || lower == "speech" || lower == "tty" || lower == "braille") {
        return false;
    }
    wrapped_media_out = std::string(raw_media);
    return true;
}

bool load_css_resource(
    const std::string& raw_href,
    const ResourceRequestContext& ctx,
    StyleSheetCache* cache,
    std::string& css_out,
    std::string& final_url_out
) {
    css_out.clear();
    final_url_out.clear();
    const std::string resolved_key = ResourceLoader::resolve_url(ctx.base_url, raw_href);
    if (resolved_key.empty()) return false;

    if (cache) {
        auto it = cache->by_url.find(resolved_key);
        if (it != cache->by_url.end()) {
            if (!it->second.ok) return false;
            css_out = it->second.css_text;
            final_url_out = it->second.final_url;
            return true;
        }
        if (cache->external_fetches >= StyleSheetCache::kMaxExternalFetches) {
            return false;
        }
        ++cache->external_fetches;
    }

    ResourceResponse resp = ResourceLoader::load_for_page(raw_href, ctx);
    if (!resp.success) {
        if (cache) {
            cache->by_url[resolved_key] = StyleSheetCache::Entry{false, {}, {}};
        }
        return false;
    }

    // Cross-origin stylesheets loaded in no-cors mode must not adopt non-CSS
    // error HTML/JSON responses if a MIME type was explicitly non-CSS on 2xx
    // cross-origin (CSS2.1 / HTML CSS-scary-data protection).
    if (resp.cross_origin && !resp.content_type.empty()) {
        MimeType mime;
        if (MimeType::parse(resp.content_type, mime)) {
            if (!(mime.type == "text" && mime.subtype == "css")) {
                if (cache) cache->by_url[resolved_key] = StyleSheetCache::Entry{false, {}, {}};
                return false;
            }
        }
    }

    std::string charset_label;
    MimeType mime;
    if (MimeType::parse(resp.content_type, mime)) {
        if (const std::string* cs = mime.parameter("charset")) charset_label = *cs;
    }
    css_out = charset_label.empty()
                  ? resp.body
                  : decode_text(resp.body, encoding_name_for_label(charset_label));
    final_url_out = resp.url.empty() ? resolved_key : resp.url;
    if (cache) {
        cache->by_url[resolved_key] = StyleSheetCache::Entry{true, final_url_out, css_out};
    }
    return true;
}

// Expands leading `@import "..."` / `@import url(...)` statements (up to depth 4)
// relative to `sheet_base_url`, preserving cascade order (imported rules precede
// the importing stylesheet's own rules).
std::string expand_css_imports(
    const std::string& css,
    const std::string& document_url,
    const std::string& sheet_base_url,
    StyleSheetCache* cache,
    std::unordered_set<std::string>& visited,
    int depth
) {
    if (depth >= 4 || css.find("@import") == std::string::npos) return css;

    std::string imported_prefix;
    std::size_t i = 0;
    const std::size_t n = css.size();

    auto skip_ws_and_comments = [&]() {
        while (i < n) {
            if (std::isspace(static_cast<unsigned char>(css[i]))) {
                ++i;
            } else if (i + 1 < n && css[i] == '/' && css[i + 1] == '*') {
                i += 2;
                while (i + 1 < n && !(css[i] == '*' && css[i + 1] == '/')) ++i;
                if (i + 1 < n) i += 2;
                else i = n;
            } else {
                break;
            }
        }
    };

    skip_ws_and_comments();
    // Optional @charset "...";
    if (i + 8 <= n && ascii_lower_str(std::string_view(css).substr(i, 8)) == "@charset") {
        auto semi = css.find(';', i);
        if (semi != std::string::npos) {
            i = semi + 1;
            skip_ws_and_comments();
        }
    }

    std::size_t body_start = i;
    while (i + 7 <= n && ascii_lower_str(std::string_view(css).substr(i, 7)) == "@import") {
        std::size_t import_pos = i;
        i += 7;
        skip_ws_and_comments();
        std::string href;
        if (i + 4 <= n && ascii_lower_str(std::string_view(css).substr(i, 4)) == "url(") {
            i += 4;
            skip_ws_and_comments();
            char quote = 0;
            if (i < n && (css[i] == '\'' || css[i] == '"')) quote = css[i++];
            std::size_t start = i;
            if (quote) {
                while (i < n && css[i] != quote) ++i;
                href = css.substr(start, i - start);
                if (i < n && css[i] == quote) ++i;
                skip_ws_and_comments();
                if (i < n && css[i] == ')') ++i;
            } else {
                while (i < n && css[i] != ')' && !std::isspace(static_cast<unsigned char>(css[i]))) ++i;
                href = css.substr(start, i - start);
                skip_ws_and_comments();
                if (i < n && css[i] == ')') ++i;
            }
        } else if (i < n && (css[i] == '\'' || css[i] == '"')) {
            char quote = css[i++];
            std::size_t start = i;
            while (i < n && css[i] != quote) ++i;
            href = css.substr(start, i - start);
            if (i < n && css[i] == quote) ++i;
        } else {
            i = import_pos;
            break;
        }

        std::size_t media_start = i;
        while (i < n && css[i] != ';' && css[i] != '{') ++i;
        std::string_view import_media = trim_ascii(std::string_view(css).substr(media_start, i - media_start));
        if (i < n && css[i] == ';') ++i;
        skip_ws_and_comments();
        body_start = i;

        if (!href.empty()) {
            std::string wrapped_media;
            if (media_matches_or_needs_wrapper(import_media, wrapped_media)) {
                ResourceRequestContext sub_ctx;
                sub_ctx.document_url = document_url;
                sub_ctx.base_url = sheet_base_url;
                sub_ctx.mode = RequestMode::NoCors;
                const std::string resolved = ResourceLoader::resolve_url(sheet_base_url, href);
                if (!resolved.empty() && visited.insert(resolved).second) {
                    std::string sub_css;
                    std::string sub_final_url;
                    if (load_css_resource(href, sub_ctx, cache, sub_css, sub_final_url)) {
                        std::string expanded = expand_css_imports(
                            sub_css, document_url, sub_final_url, cache, visited, depth + 1
                        );
                        if (!wrapped_media.empty()) {
                            imported_prefix += "@media " + wrapped_media + " {\n" + expanded + "\n}\n";
                        } else {
                            imported_prefix += expanded + "\n";
                        }
                    }
                    visited.erase(resolved);
                }
            }
        }
    }

    if (imported_prefix.empty()) return css;
    return imported_prefix + css.substr(body_start);
}

std::string extract_pi_attr(std::string_view data, std::string_view name) {
    std::size_t pos = 0;
    while (pos < data.size()) {
        while (pos < data.size() && std::isspace(static_cast<unsigned char>(data[pos]))) ++pos;
        std::size_t kstart = pos;
        while (pos < data.size() && data[pos] != '=' && !std::isspace(static_cast<unsigned char>(data[pos]))) ++pos;
        std::string_view key = data.substr(kstart, pos - kstart);
        while (pos < data.size() && std::isspace(static_cast<unsigned char>(data[pos]))) ++pos;
        if (pos >= data.size() || data[pos] != '=') {
            while (pos < data.size() && !std::isspace(static_cast<unsigned char>(data[pos]))) ++pos;
            continue;
        }
        ++pos;
        while (pos < data.size() && std::isspace(static_cast<unsigned char>(data[pos]))) ++pos;
        if (pos >= data.size() || (data[pos] != '"' && data[pos] != '\'')) continue;
        char q = data[pos++];
        std::size_t vstart = pos;
        while (pos < data.size() && data[pos] != q) ++pos;
        std::string_view val = data.substr(vstart, pos - vstart);
        if (pos < data.size()) ++pos;
        if (key == name) return std::string(val);
    }
    return {};
}

} // namespace

ResourceRequestContext DocumentStyles::request_context(
    const Document& doc,
    RequestMode mode,
    bool include_credentials
) {
    ResourceRequestContext ctx;
    ctx.document_url = doc.url();
    ctx.base_url = ctx.document_url;
    ctx.mode = mode;
    ctx.include_credentials = include_credentials;

    // HTML Standard §4.2.3: first `<base href>` in tree order sets document base URL.
    Url doc_url;
    const bool have_doc_url = Url::parse(ctx.document_url, nullptr, doc_url);
    for_each_descendant(doc, [&](const Node& node) {
        if (!node.is_element()) return true;
        const auto& el = static_cast<const Element&>(node);
        if (!el.is_foreign() && el.tag_name() == "base" && el.has_attribute("href")) {
            const std::string raw_href = std::string(trim_ascii(el.get_attribute("href")));
            Url resolved_base;
            if (Url::parse(raw_href, have_doc_url ? &doc_url : nullptr, resolved_base)) {
                ctx.base_url = resolved_base.href();
            }
            return false; // stop walk after first <base href>
        }
        return true;
    });

    return ctx;
}

StyleResolver DocumentStyles::build_resolver(
    const Document& doc,
    StyleSheetCache* cache,
    float viewport_width,
    float viewport_height
) {
    StyleResolver resolver;
    resolver.set_viewport(viewport_width, viewport_height);

    const ResourceRequestContext base_ctx = request_context(doc, RequestMode::NoCors, false);
    std::size_t sheet_count = 0;
    static constexpr std::size_t kMaxTotalSheets = 256;

    for_each_descendant(doc, [&](const Node& node) {
        if (sheet_count >= kMaxTotalSheets) return false;

        if (node.is_processing_instruction()) {
            const auto& pi = static_cast<const ProcessingInstruction&>(node);
            if (pi.target() == "xml-stylesheet") {
                const std::string type = extract_pi_attr(pi.data(), "type");
                const std::string href = std::string(trim_ascii(extract_pi_attr(pi.data(), "href")));
                const std::string alternate = ascii_lower_str(extract_pi_attr(pi.data(), "alternate"));
                const std::string media = extract_pi_attr(pi.data(), "media");
                std::string wrapped_media;
                if (!href.empty() && alternate != "yes" && is_css_type_attr(type) &&
                    media_matches_or_needs_wrapper(media, wrapped_media)) {
                    std::string css_text;
                    std::string final_url;
                    if (load_css_resource(href, base_ctx, cache, css_text, final_url)) {
                        std::unordered_set<std::string> visited{final_url};
                        std::string expanded = expand_css_imports(
                            css_text, base_ctx.document_url, final_url, cache, visited, 0
                        );
                        if (!wrapped_media.empty()) {
                            expanded = "@media " + wrapped_media + " {\n" + expanded + "\n}";
                        }
                        resolver.add_stylesheet(CssParser::parse(expanded));
                        ++sheet_count;
                    }
                }
            }
            return true;
        }

        if (!node.is_element()) return true;
        const auto& el = static_cast<const Element&>(node);
        const std::string& tag = el.tag_name();

        if (tag == "style") {
            if (el.has_attribute("disabled")) return true;
            if (!is_css_type_attr(el.get_attribute("type"))) return true;
            std::string wrapped_media;
            if (!media_matches_or_needs_wrapper(el.get_attribute("media"), wrapped_media)) return true;

            std::string css_text = el.text_content();
            std::unordered_set<std::string> visited;
            std::string expanded = expand_css_imports(
                css_text, base_ctx.document_url, base_ctx.base_url, cache, visited, 0
            );
            if (!wrapped_media.empty()) {
                expanded = "@media " + wrapped_media + " {\n" + expanded + "\n}";
            }
            resolver.add_stylesheet(CssParser::parse(expanded));
            ++sheet_count;
            return true;
        }

        if (tag == "link") {
            if (el.has_attribute("disabled")) return true;
            if (!is_stylesheet_rel(el.get_attribute("rel"))) return true;
            if (!is_css_type_attr(el.get_attribute("type"))) return true;
            const std::string href = std::string(trim_ascii(el.get_attribute("href")));
            if (href.empty()) return true;
            std::string wrapped_media;
            if (!media_matches_or_needs_wrapper(el.get_attribute("media"), wrapped_media)) return true;

            ResourceRequestContext link_ctx = base_ctx;
            if (el.has_attribute("crossorigin")) {
                link_ctx.mode = RequestMode::Cors;
                if (ascii_lower_str(trim_ascii(el.get_attribute("crossorigin"))) == "use-credentials") {
                    link_ctx.include_credentials = true;
                }
            }

            std::string css_text;
            std::string final_url;
            if (load_css_resource(href, link_ctx, cache, css_text, final_url)) {
                std::unordered_set<std::string> visited{final_url};
                std::string expanded = expand_css_imports(
                    css_text, link_ctx.document_url, final_url, cache, visited, 0
                );
                if (!wrapped_media.empty()) {
                    expanded = "@media " + wrapped_media + " {\n" + expanded + "\n}";
                }
                resolver.add_stylesheet(CssParser::parse(expanded));
                ++sheet_count;
            }
            return true;
        }

        return true;
    });

    return resolver;
}

} // namespace litebird
