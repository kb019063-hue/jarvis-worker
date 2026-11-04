#pragma once

#include <string>
#include <string_view>
#include <memory>

namespace litebird {

struct ResourceResponse {
    bool success{false};
    int status_code{0};
    std::string content_type;
    std::string body;
    std::string error_message;
};

class ResourceLoader {
public:
    static std::string resolve_url(std::string_view base_url, std::string_view relative_url);
    static ResourceResponse load(const std::string& url);
    static ResourceResponse load_file(const std::string& file_path);
    static ResourceResponse load_http(const std::string& url);

    static std::string detect_mime_type(std::string_view path_or_url);
};

} // namespace litebird
