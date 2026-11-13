#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace litebird {

/// A URL, parsed the way the WHATWG URL Standard says browsers parse one,
/// and kept in its parts.
///
/// This is what `new URL()`, `location`, `a.href`, `form.action` and
/// `history.pushState` go through, so they all agree on what an address
/// means: `"?q=1"` against `https://a.test/dir/page.html?x#y` is
/// `https://a.test/dir/page.html?q=1`, `"//cdn.test/lib.js"` keeps the scheme
/// and changes the host, `mailto:ann@x.test` has no host at all, a space in a
/// path is written `%20`, and `"nope"` with nothing to be relative to is not
/// a URL.
///
/// Not done: a host name with letters outside ASCII is kept as written and
/// not converted to its `xn--` form.
struct Url {
    std::string scheme;                 ///< "https": lower case, no colon
    std::string username;
    std::string password;
    bool has_host{false};               ///< false for mailto:, data:, about:
    std::string host;                   ///< as written out: "a.test", "[::1]", "" in file:///x
    int port{-1};                       ///< -1 when it is the scheme's own port
    bool opaque_path{false};            ///< the path is one string (mailto:, data:, javascript:)
    std::string opaque;
    std::vector<std::string> path;      ///< the segments otherwise
    bool has_query{false};
    std::string query;
    bool has_fragment{false};
    std::string fragment;

    /// Parses `input`, relative to `base` when there is one. False when it
    /// is not a URL; `out` is then unspecified.
    static bool parse(std::string_view input, const Url* base, Url& out);
    /// The same with the base as text; an empty base is no base, and a base
    /// that is not a URL makes the result a failure.
    static bool parse(std::string_view input, std::string_view base, Url& out);

    /// http, https, ws, wss, ftp and file: the schemes with a host and a
    /// path of segments, where `\` means `/`.
    bool is_special() const;

    // --- As text ----------------------------------------------------------------
    std::string href(bool without_fragment = false) const;
    /// "https://a.test:8080"; "null" for a scheme that has no origin of its
    /// own (file:, data:, about:).
    std::string origin() const;
    std::string protocol() const;       ///< "https:"
    std::string host_and_port() const;  ///< "a.test:8080"
    std::string hostname() const;
    std::string port_text() const;      ///< "" for the scheme's own port
    std::string pathname() const;
    std::string search() const;         ///< "" or "?…"
    std::string hash() const;           ///< "" or "#…"

    // --- The setters of the URL API ----------------------------------------------
    //
    // Each parses its argument with the rules of that part and changes
    // nothing when the value cannot be one (`port = "x"`), exactly as
    // assigning to `url.port` does.
    void set_protocol(std::string_view value);
    void set_username(std::string_view value);
    void set_password(std::string_view value);
    void set_host(std::string_view value);
    void set_hostname(std::string_view value);
    void set_port(std::string_view value);
    void set_pathname(std::string_view value);
    void set_search(std::string_view value);
    void set_hash(std::string_view value);

    /// True when the two differ in nothing but path, query and fragment:
    /// the test for `history.pushState`.
    bool same_site_parts(const Url& other) const;
};

/// The file: URL of an absolute path: "/home/a b/x#1.html" is
/// "file:///home/a%20b/x%231.html". Every character that would mean
/// something else in a URL is written as %XX.
std::string file_url_from_path(std::string_view absolute_path);

/// The pairs of an `application/x-www-form-urlencoded` string: a query
/// string, or the body of a form post. No leading "?" is expected.
std::vector<std::pair<std::string, std::string>> parse_urlencoded(std::string_view text);
/// The reverse.
std::string serialize_urlencoded(const std::vector<std::pair<std::string, std::string>>& pairs);

} // namespace litebird
