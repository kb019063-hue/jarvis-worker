#include "litebird/css/DocumentStyles.hpp"
#include "litebird/css/CssParser.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/dom/ProcessingInstruction.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/net/DataUrl.hpp"
#include "litebird/net/UrlParser.hpp"
#include "litebird/render/FontRenderer.hpp"

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

// CSS Syntax Level 3 §3.2: exact `@charset "<label>";` byte prefix within the first 1024 bytes.
std::string sniff_css_at_charset(std::string_view bytes) {
    static constexpr std::string_view kPrefix = "@charset \"";
    const std::string_view head = bytes.substr(0, std::min<std::size_t>(bytes.size(), 1024));
    if (head.size() < kPrefix.size() + 2 || head.compare(0, kPrefix.size(), kPrefix) != 0) {
        return {};
    }
    const std::size_t end = head.find("\";", kPrefix.size());
    if (end == std::string_view::npos) {
        return {};
    }
    std::string canon = encoding_name_for_label(head.substr(kPrefix.size(), end - kPrefix.size()));
    if (canon == "UTF-16BE" || canon == "UTF-16LE") {
        canon = "UTF-8";
    }
    return canon;
}

/// How many `@font-face` rules one document may register. A page that declares
/// more is not refused its fonts one by one; the cap is what stops a hostile
/// sheet from filling the registry, and the number is far above what a real
/// page uses (a handful of weights of one or two families).
constexpr std::size_t kMaxFontFacesPerDocument = 64;

/// Whether the engine can read the format a source claims. A `format()` hint
/// is a promise about the bytes, and a promise of `woff` or `woff2` is a
/// promise this engine cannot keep -- it reads TrueType `glyf` outlines and
/// nothing else -- so such a source is skipped rather than fetched for the
/// parser to reject. A source that claims nothing is fetched and judged by
/// what it actually is, which is the common case.
bool font_format_is_readable(const std::string& format) {
    return format.empty() || format == "truetype" || format == "opentype" ||
           format == "collection" || format == "ttf" || format == "otf";
}

/// Registers a document's `@font-face` rules with the rasterizer.
///
/// Nothing is fetched here. Each source becomes a closure that knows its URL
/// and the origin policy to fetch it under, and the rasterizer calls the
/// closures only when a face is first wanted -- a sheet that declares twenty
/// families has usually drawn in one of them by the time layout is over, and a
/// webfont is a network round trip.
void register_font_faces(const StyleSheet& sheet, const std::string& base_url,
                         const ResourceRequestContext& ctx, std::size_t& count) {
    FontRenderer& fr = FontRenderer::instance();
    for (const FontFaceRule& rule : sheet.font_faces) {
        if (count >= kMaxFontFacesPerDocument) return;
        if (rule.sources.empty()) continue;

        std::vector<FontRenderer::WebfontFetcher> sources;
        for (const FontFaceSrc& src : rule.sources) {
            if (!font_format_is_readable(src.format)) continue;

            if (!src.local.empty()) {
                // A font already on the host: `local()` is answered out of the
                // registry, and a name with no face behind it is simply a
                // source that was not available, not an error.
                const std::string path = fr.face_path(src.local);
                if (path.empty()) continue;
                sources.push_back([path]() -> std::vector<std::uint8_t> {
                    const ResourceResponse r = ResourceLoader::load_file(path);
                    if (!r.success) return {};
                    return std::vector<std::uint8_t>(r.body.begin(), r.body.end());
                });
                continue;
            }

            const std::string url = ResourceLoader::resolve_url(base_url, src.url);
            if (url.empty()) continue;

            ResourceRequestContext font_ctx = ctx;
            // CSS Fonts asks for fonts in CORS mode even where an image would
            // not be: a font is read by the page that fetched it, its bytes
            // end up in a texture the document can sample, so a cross-origin
            // font has to say so. Nothing is loosened -- a sheet that already
            // asked for stricter handling keeps it.
            if (font_ctx.mode == RequestMode::NoCors) font_ctx.mode = RequestMode::Cors;

            sources.push_back([url, font_ctx]() -> std::vector<std::uint8_t> {
                const ResourceResponse r = ResourceLoader::load_for_page(url, font_ctx);
                if (!r.success) return {};
                return std::vector<std::uint8_t>(r.body.begin(), r.body.end());
            });
        }

        if (sources.empty()) continue;
        fr.register_webfont(rule.family, std::move(sources), rule.weight_min,
                            rule.weight_max, rule.italic);
        ++count;
    }
}

bool load_css_resource(
    const std::string& raw_href,
    const ResourceRequestContext& ctx,
    const std::string& env_charset,
    StyleSheetCache* cache,
    std::string& css_out,
    std::string& final_url_out,
    std::string& sheet_charset_out
) {
    css_out.clear();
    final_url_out.clear();
    sheet_charset_out = "UTF-8";
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

    std::string chosen_charset;
    MimeType mime;
    if (MimeType::parse(resp.content_type, mime)) {
        if (const std::string* cs = mime.parameter("charset")) {
            chosen_charset = encoding_name_for_label(*cs);
        }
    }
    if (chosen_charset.empty()) {
        chosen_charset = sniff_css_at_charset(resp.body);
    }
    if (chosen_charset.empty() && !env_charset.empty()) {
        chosen_charset = encoding_name_for_label(env_charset);
    }
    if (chosen_charset.empty()) {
        chosen_charset = "UTF-8";
    }
    sheet_charset_out = chosen_charset;
    css_out = decode_text(resp.body, chosen_charset);
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
    const std::string& sheet_charset,
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
                    std::string sub_charset;
                    if (load_css_resource(href, sub_ctx, sheet_charset, cache, sub_css, sub_final_url, sub_charset)) {
                        std::string expanded = expand_css_imports(
                            sub_css, document_url, sub_final_url, sub_charset, cache, visited, depth + 1
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
    std::size_t font_face_count = 0;
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
                std::string pi_env_cs = encoding_name_for_label(extract_pi_attr(pi.data(), "charset"));
                if (pi_env_cs.empty()) pi_env_cs = doc.character_set();
                std::string wrapped_media;
                if (!href.empty() && alternate != "yes" && is_css_type_attr(type) &&
                    media_matches_or_needs_wrapper(media, wrapped_media)) {
                    std::string css_text;
                    std::string final_url;
                    std::string sheet_cs;
                    if (load_css_resource(href, base_ctx, pi_env_cs, cache, css_text, final_url, sheet_cs)) {
                        std::unordered_set<std::string> visited{final_url};
                        std::string expanded = expand_css_imports(
                            css_text, base_ctx.document_url, final_url, sheet_cs, cache, visited, 0
                        );
                        if (!wrapped_media.empty()) {
                            expanded = "@media " + wrapped_media + " {\n" + expanded + "\n}";
                        }
                        const StyleSheet parsed = CssParser::parse(expanded);
                        resolver.add_stylesheet(parsed);
                        register_font_faces(parsed, final_url, base_ctx, font_face_count);
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
                css_text, base_ctx.document_url, base_ctx.base_url, doc.character_set(), cache, visited, 0
            );
            if (!wrapped_media.empty()) {
                expanded = "@media " + wrapped_media + " {\n" + expanded + "\n}";
            }
            const StyleSheet parsed = CssParser::parse(expanded);
            resolver.add_stylesheet(parsed);
            register_font_faces(parsed, base_ctx.base_url, base_ctx, font_face_count);
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

            std::string link_env_cs = encoding_name_for_label(el.get_attribute("charset"));
            if (link_env_cs.empty()) link_env_cs = doc.character_set();

            std::string css_text;
            std::string final_url;
            std::string sheet_cs;
            if (load_css_resource(href, link_ctx, link_env_cs, cache, css_text, final_url, sheet_cs)) {
                std::unordered_set<std::string> visited{final_url};
                std::string expanded = expand_css_imports(
                    css_text, link_ctx.document_url, final_url, sheet_cs, cache, visited, 0
                );
                if (!wrapped_media.empty()) {
                    expanded = "@media " + wrapped_media + " {\n" + expanded + "\n}";
                }
                const StyleSheet parsed = CssParser::parse(expanded);
                resolver.add_stylesheet(parsed);
                register_font_faces(parsed, final_url, link_ctx, font_face_count);
                ++sheet_count;
            }
            return true;
        }

        return true;
    });

    // What a script built and the document adopted (`document.adoptedStyleSheets`),
    // in adoption order, after the document's own sheets -- the order CSSOM
    // gives them, so a later adopted rule wins over an earlier one at equal
    // specificity and both win over the document's.
    //
    // The CSSOM keeps its own model of a sheet, which this file cannot see: the
    // two models are different types, and the text a sheet was written from is
    // not kept after `insertRule`. So the record carries the sheet written back
    // to CSS, and it is parsed here exactly as a `<style>`'s text is. An
    // `@media` rule inside it needs no wrapper: `add_stylesheet` reads the
    // media rules the parser found.
    for (const auto& adopted : doc.adopted_sheets()) {
        if (sheet_count >= kMaxTotalSheets) break;
        if (!adopted || adopted->text.empty()) continue;
        const StyleSheet parsed = CssParser::parse(adopted->text);
        resolver.add_stylesheet(parsed);
        register_font_faces(parsed, doc.url(), base_ctx, font_face_count);
        ++sheet_count;
    }

    return resolver;
}

// --- The linked stylesheet, for the script-facing cascade --------------------
//
// `build_resolver` walks the tree and loads every `<style>` and
// `<link rel=stylesheet>` into a StyleResolver, which is what the layout
// paints. A script asking `getComputedStyle` reads a second cascade of its own
// (DomStyleBindings.cpp), and that one only ever collected `<style>` elements:
// a `<link>` has no text content, so the sheet a page actually links came out
// empty and every rule in it was missing from what script was told. The two
// cascades must agree, so this is the same fetch under the same rules.

bool DocumentStyles::is_stylesheet_rel(std::string_view rel) {
    bool has_stylesheet = false;
    bool has_alternate = false;
    std::size_t i = 0;
    while (i < rel.size()) {
        while (i < rel.size() && std::isspace(static_cast<unsigned char>(rel[i]))) ++i;
        const std::size_t start = i;
        while (i < rel.size() && !std::isspace(static_cast<unsigned char>(rel[i]))) ++i;
        if (i > start) {
            const std::string tok = ascii_lower_str(rel.substr(start, i - start));
            if (tok == "stylesheet") has_stylesheet = true;
            else if (tok == "alternate") has_alternate = true;
        }
    }
    return has_stylesheet && !has_alternate;
}

bool DocumentStyles::load_linked_stylesheet(
    const Element& link,
    const Document& doc,
    StyleSheetCache* cache,
    std::string& css_out
) {
    css_out.clear();
    if (link.tag_name() != "link") return false;
    if (link.has_attribute("disabled")) return false;
    if (!is_stylesheet_rel(link.get_attribute("rel"))) return false;
    if (!is_css_type_attr(link.get_attribute("type"))) return false;
    const std::string href = std::string(trim_ascii(link.get_attribute("href")));
    if (href.empty()) return false;
    std::string wrapped_media;
    if (!media_matches_or_needs_wrapper(link.get_attribute("media"), wrapped_media)) return false;

    ResourceRequestContext ctx = request_context(doc, RequestMode::NoCors, false);
    if (link.has_attribute("crossorigin")) {
        ctx.mode = RequestMode::Cors;
        if (ascii_lower_str(trim_ascii(link.get_attribute("crossorigin"))) == "use-credentials") {
            ctx.include_credentials = true;
        }
    }
    std::string env_cs = encoding_name_for_label(link.get_attribute("charset"));
    if (env_cs.empty()) env_cs = doc.character_set();

    std::string css_text;
    std::string final_url;
    std::string sheet_cs;
    if (!load_css_resource(href, ctx, env_cs, cache, css_text, final_url, sheet_cs)) return false;
    std::unordered_set<std::string> visited{final_url};
    std::string expanded = expand_css_imports(css_text, ctx.document_url, final_url, sheet_cs, cache, visited, 0);
    if (!wrapped_media.empty()) expanded = "@media " + wrapped_media + " {\n" + expanded + "\n}";
    css_out = std::move(expanded);
    return true;
}

} // namespace litebird
