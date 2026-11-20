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

/// WHATWG Fetch / RFC 9111 cache mode for HTTP(S) loads.
enum class CacheMode {
    Default,
    NoStore,
    Reload,
    NoCache,
    ForceCache
};

struct ResourceResponse {
    /// True when the resource was successfully retrieved AND HTTP status is 2xx/3xx.
    bool success{false};
    /// True when a valid response arrived and passed origin/CORS policy (even for HTTP 4xx/5xx).
    bool network_ok{false};
    bool cross_origin{false};
    /// True when satisfied from the RFC 9111 HTTP cache (either fresh hit or 304 revalidation).
    bool from_cache{false};
    /// True when a conditional `If-None-Match` / `If-Modified-Since` check received `304 Not Modified`.
    bool revalidated{false};
    int status_code{0};
    std::string status_text;
    std::string content_type;
    std::unordered_map<std::string, std::string> headers;
    std::vector<std::pair<std::string, std::string>> header_list;
    std::string body;
    std::string error_message;
    /// The final address actually loaded (after any redirects), without a fragment.
    std::string url;

    /// For a `401`, what the server asked for: the schemes it offered in
    /// `WWW-Authenticate`, in the order it wrote them ("Basic", "Digest").
    /// Empty for every other status, and for a 401 whose header is missing or
    /// unreadable. A page cannot read this; it is for the embedder, which is
    /// what decides whether to fill the credential store.
    std::vector<std::string> auth_schemes;
    /// The `realm` parameter of the first challenge that has one. Empty when
    /// none did.
    std::string auth_realm;
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
    /// When true (`credentials: "omit"`), cookies are neither sent nor stored
    /// even on same-origin requests. When both `include_credentials` and
    /// `omit_credentials` are false, Fetch `"same-origin"` credentials apply.
    bool omit_credentials{false};
    CacheMode cache_mode{CacheMode::Default};
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

    /// Performs an HTTP/1.1 or HTTPS (TLS) request with persistent keep-alive
    /// socket pooling, following redirects, consulting/updating the HTTP cache
    /// and CookieJar, and decoding chunked transfer-encoding and
    /// gzip/deflate/br content-encoding.
    static ResourceResponse load_http(
        const std::string& url,
        const std::string& method = "GET",
        const std::unordered_map<std::string, std::string>& headers = {},
        const std::string& body = ""
    );

    /// Single-hop HTTP/HTTPS wire request without redirect following, cookie
    /// injection, or cache lookup.
    static ResourceResponse load_http_single_hop(
        const std::string& url,
        const std::string& method = "GET",
        const std::unordered_map<std::string, std::string>& headers = {},
        const std::string& body = ""
    );

    /// Credentials for an HTTP authentication realm (RFC 7617 §2.2).
    ///
    /// Nothing is sent until a server asks: the first request for an origin
    /// carries no `Authorization`, a `401` is read for its challenges, and the
    /// request is tried once more with credentials for that origin *and* that
    /// realm. A credential set for one realm is never offered to another, which
    /// is what keeps a page from collecting a password meant for elsewhere.
    static void set_auth_credentials(const std::string& origin, const std::string& realm,
                                     const std::string& username, const std::string& password);

    /// The credentials held for an origin and realm, if any. False when none
    /// are held; `has_credentials` is the question, not an error.
    static bool has_credentials(const std::string& origin, const std::string& realm);

    /// Forgets every credential held, for every origin and realm.
    static void clear_auth_credentials();
    /// How many (origin, realm) pairs are held. For tests.
    static std::size_t auth_credential_count();

    /// Clears both the RFC 9111 HTTP response cache and the CORS preflight cache.
    static void clear_http_cache();
    static std::size_t http_cache_size();
    static std::size_t cors_preflight_cache_size();

    /// Clears all idle HTTP/1.1 and HTTPS sockets held in the keep-alive pool.
    static void clear_http_connection_pool();
    static std::size_t http_connection_pool_size();

    /// Optional custom CA bundle path for deterministic loopback HTTPS testing.
    static void set_ca_bundle_for_testing(std::string ca_file_path);

    /// Maps an HTTP(S) origin (e.g. "https://web-platform.test") to a local
    /// directory root so offline test suites can load subresources, frames, and
    /// scripts over real HTTP(S) origins without opening network sockets.
    static void set_origin_root_for_testing(const std::string& origin, std::string directory_path);
    static void clear_origin_roots_for_testing();

    static std::string detect_mime_type(std::string_view path_or_url);

    // --- HTTP authentication, for tests and for embedders that fill the store --
    //
    // The digests are RFC 1321 MD5 and FIPS 180-4 SHA-256, and RFC 7616 §3.9.1
    // publishes the answers, so these are exposed rather than hidden: a test
    // can hold the implementation to a value it did not compute. They are the
    // same functions the loader uses, not a second copy.
    static std::string auth_md5_hex(std::string_view data);
    static std::string auth_sha256_hex(std::string_view data);
    static std::string auth_base64(std::string_view bytes);
    /// The value a client sends, or false when the challenge cannot be
    /// answered (an unknown algorithm, `auth-int` only, no credentials held).
    /// `cnonce` is an argument so that a test can reproduce RFC 7616 §3.9.1
    /// exactly; the loader generates a fresh one.
    static bool auth_Digest_response(const std::string& algorithm,
                                     const std::string& username, const std::string& password,
                                     const std::string& realm, const std::string& nonce,
                                     const std::string& method, const std::string& request_target,
                                     const std::string& qop, const std::string& cnonce,
                                     const std::string& nc, std::string& out_hex);
};

} // namespace litebird
