#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>

namespace litebird {

struct ResourceResponse {
    bool success{false};
    int status_code{0};
    std::string content_type;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::string error_message;
    /// The address actually loaded, without a fragment. Empty when the
    /// request was refused before a scheme processor ran.
    std::string url;
};

/// Where a page-initiated request comes from. `document_url` decides what
/// the page may read; `base_url` only resolves relative references (a
/// `<base href>` may change it, but must not grant the document's origin).
/// An empty or invalid document URL is an opaque context: data: only.
struct ResourceRequestContext {
    std::string document_url;
    std::string base_url;
};

class ResourceLoader {
public:
    static constexpr std::size_t MaxHttpResponseBytes = 16u * 1024u * 1024u;
    static constexpr std::size_t MaxHttpHeaderBytes = 64u * 1024u;

    static std::string resolve_url(std::string_view base_url, std::string_view relative_url);
    /// The address a page loaded from `location` has, as its scripts see it
    /// (location.href): the URL itself when it is one, and the file: URL of
    /// the absolute path when it is a path on disk.
    static std::string address_of(const std::string& location);

    /// Load a URL a script asked for. The context is required: there is no
    /// default that reads a bare path as a file. Web documents may not read
    /// file: URLs or local paths; a document the embedder loaded from file:
    /// keeps local-file access. data: is self-contained and always allowed.
    static ResourceResponse load_for_page(
        const std::string& url,
        const ResourceRequestContext& context,
        const std::string& method = "GET",
        const std::unordered_map<std::string, std::string>& headers = {},
        const std::string& body = ""
    );

    /// Embedder-only navigation (CLI/TUI address bar, history, reload). A
    /// bare path is a local file here. Never call this with a URL a page
    /// supplied without first going through load_for_page and its context.
    static ResourceResponse load_navigation(const std::string& url);

    static ResourceResponse load_file(const std::string& file_path);
    static ResourceResponse load_http(
        const std::string& url,
        const std::string& method = "GET",
        const std::unordered_map<std::string, std::string>& headers = {},
        const std::string& body = ""
    );

    static std::string detect_mime_type(std::string_view path_or_url);
};

} // namespace litebird
