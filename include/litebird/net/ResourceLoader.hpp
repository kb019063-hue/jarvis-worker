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
    /// The scheme a `407` offered (RFC 9110 §11.7.1), which is proxy
    /// authentication and not this document's. Empty when the response was not
    /// a 407, or carried no readable `Proxy-Authenticate`.
    std::vector<std::string> proxy_auth_schemes;
    /// The `realm` of the first proxy challenge that has one.
    std::string proxy_auth_realm;
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
    /// Honors `HTTP_PROXY`/`http_proxy`, `HTTPS_PROXY`/`https_proxy`,
    /// `ALL_PROXY`/`all_proxy`, and `NO_PROXY`/`no_proxy` (`http://` uses
    /// absolute-form requests; `https://` uses HTTP `CONNECT` tunnels).
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
    /// and CookieJar, honoring HTTP(S) proxy environment variables, and
    /// decoding chunked transfer-encoding and gzip/deflate/br content-encoding.
    static ResourceResponse load_http(
        const std::string& url,
        const std::string& method = "GET",
        const std::unordered_map<std::string, std::string>& headers = {},
        const std::string& body = ""
    );

    /// Single-hop HTTP/HTTPS wire request without redirect following, cookie
    /// injection, or cache lookup. Routes `http://` via absolute-form and
    /// `https://` via HTTP `CONNECT` when configured proxy env vars apply.
    static ResourceResponse load_http_single_hop(
        const std::string& url,
        const std::string& method = "GET",
        const std::unordered_map<std::string, std::string>& headers = {},
        const std::string& body = ""
    );

    /// Credentials for an HTTP authentication realm (RFC 7617 §2.2).
    ///
    /// Nothing is sent until a server asks: the first request carries no
    /// `Authorization`, and a `401` may be answered with credentials for that
    /// origin *and* that realm. A same-realm Digest `stale=true` response gets
    /// one bounded retry with its replacement nonce; it does not discard the
    /// credential. A non-stale refusal does. A credential set for one realm is
    /// never offered to another, which keeps a page from collecting a password
    /// meant for elsewhere.
    static void set_auth_credentials(const std::string& origin, const std::string& realm,
                                     const std::string& username, const std::string& password);

    /// The credentials held for an origin and realm, if any. False when none
    /// are held; `has_credentials` is the question, not an error.
    static bool has_credentials(const std::string& origin, const std::string& realm);

    /// Credentials for a **proxy** authentication realm (RFC 9110 §11.7).
    ///
    /// A proxy's protection space is its own: RFC 9110 §11.7.1 gives it
    /// `Proxy-Authenticate` on a `407` and `Proxy-Authorization` on the
    /// request, and §11.7 says the two spaces are kept apart so that a proxy
    /// can never collect credentials meant for an origin (or the reverse).
    /// `authority` is the proxy's authority as the client names it --
    /// `host` or `host:port` -- and `realm` is the realm its challenge
    /// carried. Nothing is sent until a proxy asks, and a non-stale `407`
    /// after an attempt forgets the pair exactly as a `401` does.
    static void set_proxy_credentials(const std::string& authority, const std::string& realm,
                                      const std::string& username, const std::string& password);

    /// The proxy credentials held for an authority and realm, if any.
    static bool has_proxy_credentials(const std::string& authority, const std::string& realm);

    /// Forgets every proxy credential held, for every authority and realm.
    /// Separate from `clear_auth_credentials`, which is the document's.
    static void clear_proxy_credentials();
    /// How many (authority, realm) proxy pairs are held. For tests.
    static std::size_t proxy_credential_count();

    /// Resolves a `407` proxy challenge during an HTTP `CONNECT` tunnel
    /// handshake (`WebSocket` and `ResourceLoader`). Reads only the proxy
    /// credential store (`set_proxy_credentials`) and updates proxy Digest
    /// nonce state. `attempt` starts at 0 (`0` = initial 407, `1` = post-auth
    /// 407, `2` = post-stale-retry 407); `attempted_realm`, `attempted_scheme`,
    /// and `matched_authority` carry state across attempts. On a non-stale
    /// `407` after an attempt, the attempted proxy credential is forgotten.
    static bool resolve_proxy_connect_407(
        const std::string& proxy_authority,
        const std::string& target_authority,
        const std::vector<std::pair<std::string, std::string>>& response_headers,
        int attempt,
        std::string& attempted_realm,
        std::string& attempted_scheme,
        std::string& matched_authority,
        std::string& out_authorization);

    /// Records `Proxy-Authentication-Info` `nextnonce` after a successful
    /// `2xx` `CONNECT` response.
    static void note_proxy_connect_success(
        const std::string& matched_authority,
        const std::string& realm,
        const std::vector<std::pair<std::string, std::string>>& response_headers);

    /// Forgets every credential held, for every origin and realm -- and, for
    /// the document space only, every proxy credential too. A caller that
    /// means "all of it" gets all of it.
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
    /// FIPS 180-4 §6.7 SHA-512/256 — the `SHA-512-256` of RFC 7616 §3.2, which
    /// is the SHA-512 compression function with its own IV and a 256-bit
    /// truncation, *not* SHA-512's output cut in half.
    static std::string auth_sha512_256_hex(std::string_view data);
    static std::string auth_base64(std::string_view bytes);
    /// The `nextnonce` remembered for a protection space, or empty. RFC 7616
    /// §3.5: `Authentication-Info` names the nonce to use next, and a server
    /// issuing one-time nonces requires it.
    static std::string auth_next_nonce_for_testing(const std::string& origin, const std::string& realm);
    /// The response hex alone, for the RFC 7616 §3.9.1 and §3.9.2 examples.
    /// `cnonce` is an argument so a test can reproduce them exactly; the
    /// loader generates a fresh one.
    static bool auth_Digest_response(const std::string& algorithm,
                                     const std::string& username, const std::string& password,
                                     const std::string& realm, const std::string& nonce,
                                     const std::string& method, const std::string& request_target,
                                     const std::string& qop, const std::string& cnonce,
                                     const std::string& nc, std::string& out_hex);
    /// The whole `Authorization` value, which is what the §3.9.2 example needs:
    /// it carries `opaque`, sends the username hashed (`userhash=true`) when
    /// `hash_username`, and digests `entity_body` under `qop=auth-int`. Empty
    /// `qop` is the RFC 2069 form. False when the algorithm is not one of
    /// MD5, SHA-256, SHA-512-256 (each optionally `-sess`), or when `qop` is
    /// neither empty, `auth`, nor `auth-int`.
    static bool auth_Digest_build(const std::string& algorithm,
                                  const std::string& username, const std::string& password,
                                  const std::string& realm, const std::string& nonce,
                                  const std::string& opaque,
                                  const std::string& method, const std::string& request_target,
                                  const std::string& qop, const std::string& cnonce,
                                  const std::string& nc,
                                  std::string_view entity_body, bool hash_username,
                                  std::string& out);
};

} // namespace litebird
