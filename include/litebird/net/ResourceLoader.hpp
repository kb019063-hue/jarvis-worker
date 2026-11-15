#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace litebird {

enum class RequestMode {
    NoCors,
    Cors,
    SameOrigin
};

struct ResourceResponse {
    /// True when the resource was successfully retrieved AND HTTP status is 2xx/3xx.
    bool success{false};
    /// True when a valid response arrived and passed origin/CORS policy (even for HTTP 4xx/5xx).
    bool network_ok{false};
    bool cross_origin{false};
    int status_code{0};
    std::string status_text;
    std::string content_type;
    std::unordered_map<std::string, std::string> headers;
    std::vector<std::pair<std::string, std::string>> header_list;
    std::string body;
    std::string error_message;
    /// The final address actually loaded (after any redirects), without a fragment.
    std::string url;
};

/// Where a page-initiated request comes from. `document_url` decides what
/// the page may read; `base_url` only resolves relative references (a
/// `<base href>` may change it, but must not grant the document's origin).
/// An empty or invalid document URL is an opaque context: data: only.
struct ResourceRequestContext {
    std::string document_url;
    std::string base_url;
    RequestMode mode{RequestMode::Cors};
    bool include_credentials{false};
    bool allow_local_files{true};
};

class ResourceLoader {
public:
    static constexpr std::size_t MaxHttpResponseBytes = 16u * 1024u * 1024u;
    static constexpr std::size_t MaxHttpHeaderBytes = 64u * 1024u;
    static constexpr int MaxRedirects = 10;

    static std::string resolve_url(std::string_view base_url, std::string_view relative_url);
    /// The address a page loaded from `location` has, as its scripts see it
    /// (location.href): the URL itself when it is one, and the file: URL of
    /// the absolute path when it is a path on disk.
    static std::string address_of(const std::string& location);

    /// Load a URL a script or subresource asked for. The context is required:
    /// there is no default that reads a bare path as a file. Web documents may
    /// not read file: URLs or local paths; a document the embedder loaded from
    /// file: keeps local-file access when `allow_local_files` is true.
    static ResourceResponse load_for_page(
        const std::string& url,
        const ResourceRequestContext& context,
        const std::string& method = "GET",
        const std::unordered_map<std::string, std::string>& headers = {},
        const std::string& body = ""
    );

    /// Embedder-only navigation (CLI/TUI/GUI address bar, history, reload). A
    /// bare path is a local file here. Never call this with a URL a page
    /// supplied without first going through load_for_page and its context.
    static ResourceResponse load_navigation(const std::string& url);

    static ResourceResponse load_file(const std::string& file_path);

    /// Performs an HTTP/1.1 or HTTPS (TLS) request, following redirects and
    /// decoding chunked transfer-encoding and gzip/deflate content-encoding.
    static ResourceResponse load_http(
        const std::string& url,
        const std::string& method = "GET",
        const std::unordered_map<std::string, std::string>& headers = {},
        const std::string& body = ""
    );

    /// Single-hop HTTP/HTTPS request without automatic redirect following
    /// (used internally so CORS checks run at every redirect hop).
    static ResourceResponse load_http_single_hop(
        const std::string& url,
        const std::string& method = "GET",
        const std::unordered_map<std::string, std::string>& headers = {},
        const std::string& body = ""
    );

    /// Optional custom CA bundle path for deterministic loopback HTTPS testing.
    static void set_ca_bundle_for_testing(std::string ca_file_path);

    static std::string detect_mime_type(std::string_view path_or_url);
};

} // namespace litebird
