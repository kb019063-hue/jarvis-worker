#pragma once

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
    static std::string resolve_url(std::string_view base_url, std::string_view relative_url);
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
