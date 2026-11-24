#pragma once

#include "litebird/css/StyleResolver.hpp"
#include "litebird/css/StyleSheet.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include <cstddef>
#include <string>
#include <unordered_map>

namespace litebird {

/// Per-document cache of fetched external stylesheets (keyed by resolved URL)
/// so repeated reflows (`getBoundingClientRect()`, resize, hover/click) do not
/// re-download external CSS over the network.
struct StyleSheetCache {
    struct Entry {
        bool ok{false};
        std::string final_url;
        std::string css_text;
    };
    std::unordered_map<std::string, Entry> by_url;
    std::size_t external_fetches{0};
    static constexpr std::size_t kMaxExternalFetches = 128;

    void clear() {
        by_url.clear();
        external_fetches = 0;
    }
};

class DocumentStyles {
public:
    /// Computes the document base URL (`<base href>` if present and valid,
    /// otherwise `doc.url()`) and builds an origin-aware request context.
    static ResourceRequestContext request_context(
        const Document& doc,
        RequestMode mode = RequestMode::NoCors,
        bool include_credentials = false
    );

    /// The text of the stylesheet a `<link rel="stylesheet">` points at, loaded
    /// through the same request context, cache and `@import` expansion the
    /// layout uses, so that a script asking `getComputedStyle` sees exactly the
    /// rules that were painted. Answers false for anything that is not a
    /// stylesheet link or could not be read; `css_out` is then empty.
    static bool load_linked_stylesheet(
        const Element& link,
        const Document& doc,
        StyleSheetCache* cache,
        std::string& css_out
    );

    /// True when `rel` names a stylesheet (and not an alternate one), which is
    /// the same test `build_resolver` makes before it loads a `<link>`.
    static bool is_stylesheet_rel(std::string_view rel);

    /// Collects all `<style>`, `<link rel="stylesheet" href="...">`, and
    /// `<?xml-stylesheet ...?>` rules in exact document tree order (including
    /// `@import` rules up to depth 4) into a `StyleResolver`.
    static StyleResolver build_resolver(
        const Document& doc,
        StyleSheetCache* cache = nullptr,
        float viewport_width = 1024.0f,
        float viewport_height = 768.0f
    );
};

} // namespace litebird
