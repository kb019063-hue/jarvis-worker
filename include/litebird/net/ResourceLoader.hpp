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
    static ResourceResponse load(
        const std::string& url,
        const std::string& method = "GET",
        const std::unordered_map<std::string, std::string>& headers = {},
        const std::string& body = ""
    );
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
