#include "litebird/net/ResourceLoader.hpp"
#include "litebird/net/DataUrl.hpp"
#include "litebird/net/UrlParser.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <charconv>
#include <cerrno>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

namespace litebird {
namespace {

bool is_safe_header_value(std::string_view value) {
    for (unsigned char c : value) {
        // HTAB and visible/obs-text bytes are allowed; all other controls,
        // including CR/LF, can split or alter request headers.
        if ((c < 0x20 && c != '\t') || c == 0x7f) return false;
    }
    return true;
}

bool is_safe_request_target(std::string_view value) {
    if (value.empty() || value.front() != '/') return false;
    for (unsigned char c : value) {
        // Request-target cannot contain whitespace or control bytes; in
        // particular this prevents CRLF from introducing a second request.
        if (c <= 0x20 || c == 0x7f) return false;
    }
    return true;
}

bool contains_authority_delimiter(std::string_view value) {
    for (unsigned char c : value) {
        if (c <= 0x20 || c == 0x7f || c == '/' || c == '?' || c == '#' || c == '@' || c == '\\') {
            return true;
        }
    }
    return false;
}

std::string ascii_lower(std::string_view value) {
    std::string result(value);
    for (char& c : result) {
        const auto ch = static_cast<unsigned char>(c);
        if (ch >= 'A' && ch <= 'Z') c = static_cast<char>(ch + ('a' - 'A'));
    }
    return result;
}

bool has_url_scheme(std::string_view value) {
    if (value.empty()) return false;
    const auto alpha = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    };
    if (!alpha(value.front())) return false;
    for (std::size_t i = 1; i < value.size(); ++i) {
        const char c = value[i];
        if (c == ':') return true;
        const bool scheme_char = alpha(c) || (c >= '0' && c <= '9') ||
                                 c == '+' || c == '-' || c == '.';
        if (!scheme_char) return false;
    }
    return false;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percent_decode_path(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = hex_digit(text[i + 1]);
            const int lo = hex_digit(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

std::string file_path_from_url(const Url& url) {
    if (url.scheme != "file" || !url.host.empty()) return {};
    std::string path = percent_decode_path(url.pathname());
    if (path.empty() || path.find('\0') != std::string::npos) return {};
    return path;
}

} // namespace

std::string ResourceLoader::detect_mime_type(std::string_view path_or_url) {
    auto dot_pos = path_or_url.rfind('.');
    if (dot_pos == std::string_view::npos) {
        return "text/plain";
    }
    auto ext = path_or_url.substr(dot_pos);
    std::string ext_lower;
    for (char c : ext) ext_lower += static_cast<char>(std::tolower(c));

    if (ext_lower == ".html" || ext_lower == ".htm") return "text/html";
    if (ext_lower == ".css") return "text/css";
    if (ext_lower == ".js") return "application/javascript";
    if (ext_lower == ".json") return "application/json";
    if (ext_lower == ".txt") return "text/plain";
    if (ext_lower == ".bmp") return "image/bmp";
    return "application/octet-stream";
}

std::string ResourceLoader::address_of(const std::string& location) {
    for (const char* scheme : {"http://", "https://", "file://", "data:", "about:"}) {
        if (location.rfind(scheme, 0) != 0) continue;
        Url url;
        return Url::parse(location, nullptr, url) ? url.href() : location;
    }
    std::error_code failed;
    const auto absolute = std::filesystem::absolute(location, failed);
    return file_url_from_path(failed ? location : absolute.lexically_normal().string());
}

std::string ResourceLoader::resolve_url(std::string_view base_url, std::string_view relative_url) {
    if (relative_url.empty()) return std::string(base_url);

    // If relative_url is already absolute HTTP or file URL
    if (relative_url.rfind("http://", 0) == 0 ||
        relative_url.rfind("https://", 0) == 0 ||
        relative_url.rfind("file://", 0) == 0) {
        return std::string(relative_url);
    }

    // If starts with '/', treat as root-relative
    if (relative_url.front() == '/') {
        size_t scheme_len = 0;
        if (base_url.rfind("http://", 0) == 0) scheme_len = 7;
        else if (base_url.rfind("https://", 0) == 0) scheme_len = 8;

        if (scheme_len > 0) {
            auto slash_pos = base_url.find('/', scheme_len);
            if (slash_pos == std::string_view::npos) {
                return std::string(base_url) + std::string(relative_url);
            }
            return std::string(base_url.substr(0, slash_pos)) + std::string(relative_url);
        }
        return std::string(relative_url);
    }

    // Directory-relative resolution
    std::string base_dir(base_url);
    auto last_slash = base_dir.rfind('/');
    if (last_slash != std::string::npos) {
        base_dir = base_dir.substr(0, last_slash + 1);
    } else {
        base_dir.clear();
    }

    std::string combined = base_dir + std::string(relative_url);

    // Normalize ./ and ../
    std::vector<std::string> parts;
    std::string segment;
    std::stringstream ss(combined);
    bool is_http = combined.rfind("http://", 0) == 0;
    bool is_https = combined.rfind("https://", 0) == 0;
    std::string prefix;
    if (is_http || is_https) {
        prefix = is_https ? "https://" : "http://";
        std::string without_scheme = combined.substr(prefix.size());
        std::stringstream ss2(without_scheme);
        while (std::getline(ss2, segment, '/')) {
            if (segment.empty() || segment == ".") continue;
            if (segment == "..") {
                if (!parts.empty()) parts.pop_back();
            } else {
                parts.push_back(segment);
            }
        }
        std::string res = prefix;
        for (size_t i = 0; i < parts.size(); ++i) {
            res += parts[i];
            if (i + 1 < parts.size()) res += "/";
        }
        return res;
    }

    while (std::getline(ss, segment, '/')) {
        if (segment.empty() || segment == ".") continue;
        if (segment == "..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(segment);
        }
    }

    std::string res;
    if (!combined.empty() && combined.front() == '/') res += "/";
    for (size_t i = 0; i < parts.size(); ++i) {
        res += parts[i];
        if (i + 1 < parts.size()) res += "/";
    }
    return res.empty() ? "." : res;
}

ResourceResponse ResourceLoader::load_for_page(
    const std::string& url,
    const ResourceRequestContext& context,
    const std::string& method,
    const std::unordered_map<std::string, std::string>& headers,
    const std::string& body
) {
    ResourceResponse denied;
    denied.error_message = "Blocked by resource origin policy";

    Url document;
    const bool have_document = !context.document_url.empty() &&
                               Url::parse(context.document_url, nullptr, document);

    // The base URL resolves references; it never decides what the document
    // may read. A <base href="file:///..."> in a web page is still a web page.
    Url base;
    bool have_base = false;
    if (!context.base_url.empty()) have_base = Url::parse(context.base_url, nullptr, base);
    if (!have_base && have_document) {
        base = document;
        have_base = true;
    }

    Url target;
    if (!Url::parse(url, have_base ? &base : nullptr, target)) {
        denied.error_message = "Invalid request URL";
        return denied;
    }
    const std::string address = target.href(true);

    // data: holds its own bytes and needs neither disk nor network.
    if (target.scheme == "data") {
        MimeType type;
        std::string decoded;
        if (!process_data_url(address, type, decoded)) {
            denied.error_message = "Invalid data: URL";
            return denied;
        }
        ResourceResponse resp;
        resp.success = true;
        resp.status_code = 200;
        resp.url = address;
        resp.body = std::move(decoded);
        resp.content_type = type.serialize();
        resp.headers["content-type"] = resp.content_type;
        resp.headers["content-length"] = std::to_string(resp.body.size());
        return resp;
    }

    if (!have_document) {
        denied.error_message = "Request has no document origin";
        return denied;
    }

    const bool web_document = document.scheme == "http" || document.scheme == "https";
    const bool file_document = document.scheme == "file";

    if (target.scheme == "file") {
        if (!file_document) {
            denied.error_message = "Web pages may not read local files";
            return denied;
        }
        const std::string path = file_path_from_url(target);
        if (path.empty()) {
            denied.error_message = "Unsupported file: URL host or path";
            return denied;
        }
        ResourceResponse resp = load_file(path);
        if (resp.success) resp.url = address;
        return resp;
    }

    if (target.scheme == "http") {
        if (!web_document && !file_document) {
            denied.error_message = "Opaque documents may not make network requests";
            return denied;
        }
        ResourceResponse resp = load_http(address, method, headers, body);
        if (resp.success) resp.url = address;
        return resp;
    }

    if (target.scheme == "https") {
        denied.error_message = "Unsupported protocol: https:// is not implemented";
        return denied;
    }

    denied.error_message = "Unsupported protocol: " + target.scheme + ":";
    return denied;
}

ResourceResponse ResourceLoader::load_navigation(const std::string& url) {
    Url target;
    if (Url::parse(url, nullptr, target)) {
        const std::string address = target.href(true);
        if (target.scheme == "data") {
            ResourceRequestContext context;
            return load_for_page(address, context);
        }
        if (target.scheme == "http") return load_http(address);
        if (target.scheme == "file") {
            const std::string path = file_path_from_url(target);
            if (path.empty()) {
                ResourceResponse resp;
                resp.error_message = "Unsupported file: URL host or path";
                return resp;
            }
            ResourceResponse resp = load_file(path);
            if (resp.success) resp.url = address;
            return resp;
        }
        ResourceResponse resp;
        resp.error_message = "Unsupported protocol: " + target.scheme + ":";
        return resp;
    }

    // A bare path is an embedder navigation, not a page request. Anything
    // that looks like a scheme but did not parse is refused, not opened as
    // a file with a strange name.
    if (has_url_scheme(url)) {
        ResourceResponse resp;
        resp.error_message = "Invalid navigation URL";
        return resp;
    }
    ResourceResponse resp = load_file(url);
    if (resp.success) resp.url = file_url_from_path(std::filesystem::absolute(url).lexically_normal().string());
    return resp;
}

ResourceResponse ResourceLoader::load_file(const std::string& file_path) {
    ResourceResponse resp;
    if (file_path.empty() || file_path.find('\0') != std::string::npos) {
        resp.error_message = "Invalid file path";
        return resp;
    }
    std::ifstream file(file_path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        resp.success = false;
        resp.status_code = 404;
        resp.error_message = "File not found: " + file_path;
        return resp;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    resp.body = ss.str();
    resp.status_code = 200;
    resp.success = true;
    resp.content_type = detect_mime_type(file_path);
    resp.headers["content-type"] = resp.content_type;
    resp.headers["content-length"] = std::to_string(resp.body.size());
    return resp;
}

ResourceResponse ResourceLoader::load_http(
    const std::string& url,
    const std::string& method,
    const std::unordered_map<std::string, std::string>& headers,
    const std::string& body
) {
    ResourceResponse resp;
    // Expected format: http://host[:port][/path]
    if (url.rfind("http://", 0) != 0) {
        resp.error_message = "Unsupported protocol: only http:// is supported";
        return resp;
    }

    const std::string req_method = method.empty() ? "GET" : method;
    if (!is_http_token(req_method)) {
        resp.error_message = "Invalid HTTP method token";
        return resp;
    }
    std::size_t user_header_bytes = 0;
    bool has_content_length = false;
    for (const auto& [name, value] : headers) {
        if (!is_http_token(name) || !is_safe_header_value(value)) {
            resp.error_message = "Invalid HTTP request header name or value";
            return resp;
        }
        const std::string lower_name = ascii_lower(name);
        if (lower_name == "host" || lower_name == "transfer-encoding" || lower_name == "connection") {
            resp.error_message = "HTTP request header is managed by ResourceLoader: " + lower_name;
            return resp;
        }
        if (lower_name == "content-length") {
            std::size_t declared_size = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), declared_size);
            if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                declared_size != body.size()) {
                resp.error_message = "HTTP Content-Length must match the request body size";
                return resp;
            }
            has_content_length = true;
        }
        if (name.size() > MaxHttpHeaderBytes - 4 ||
            value.size() > MaxHttpHeaderBytes - 4 - name.size() ||
            user_header_bytes > MaxHttpHeaderBytes - 4 - name.size() - value.size()) {
            resp.error_message = "HTTP request headers exceed 64 KiB limit";
            return resp;
        }
        user_header_bytes += name.size() + value.size() + 4;
    }

    std::string without_scheme = url.substr(7);
    const auto authority_end = without_scheme.find_first_of("/?#");
    std::string host_port = (authority_end == std::string::npos)
        ? without_scheme : without_scheme.substr(0, authority_end);
    if (host_port.empty() || contains_authority_delimiter(host_port)) {
        resp.error_message = "Invalid HTTP URL authority";
        return resp;
    }

    std::string path = "/";
    if (authority_end != std::string::npos) {
        if (without_scheme[authority_end] == '/') path = without_scheme.substr(authority_end);
        else if (without_scheme[authority_end] == '?') path = "/" + without_scheme.substr(authority_end);
    }
    if (const auto fragment = path.find('#'); fragment != std::string::npos) path.resize(fragment);
    if (path.empty()) path = "/";
    if (!is_safe_request_target(path)) {
        resp.error_message = "Invalid HTTP request target";
        return resp;
    }

    std::string host = host_port;
    int port = 80;
    const auto colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        if (host_port.find(':', colon_pos + 1) != std::string::npos) {
            resp.error_message = "IPv6 and malformed HTTP authorities are unsupported";
            return resp;
        }
        host = host_port.substr(0, colon_pos);
        const std::string port_text = host_port.substr(colon_pos + 1);
        unsigned parsed_port = 0;
        const auto parsed = std::from_chars(port_text.data(), port_text.data() + port_text.size(), parsed_port);
        if (host.empty() || port_text.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != port_text.data() + port_text.size() || parsed_port == 0 || parsed_port > 65535) {
            resp.error_message = "Invalid HTTP port";
            return resp;
        }
        port = static_cast<int>(parsed_port);
    }

    // Build and cap the complete request headers before resolving or opening
    // a connection. Caller-controlled pieces were validated above.
    const std::string host_header = host + (port == 80 ? "" : ":" + std::to_string(port));
    std::string request = req_method + " " + path + " HTTP/1.1\r\n" +
                          "Host: " + host_header + "\r\n" +
                          "User-Agent: Litebird/1.0 (Native Engine)\r\n" +
                          "Accept: text/html,application/xhtml+xml,application/xml,application/json;q=0.9,*/*;q=0.8\r\n";
    for (const auto& [hk, hv] : headers) request += hk + ": " + hv + "\r\n";
    if (!has_content_length && !body.empty()) request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    request += "Connection: close\r\n";
    if (request.size() + 2 > MaxHttpHeaderBytes) {
        resp.error_message = "HTTP request headers exceed 64 KiB limit";
        return resp;
    }
    request += "\r\n";
    if (!body.empty()) request += body;

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port);
    int status = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (status != 0 || !res) {
        resp.error_message = "Failed to resolve host: " + host;
        return resp;
    }

    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        freeaddrinfo(res);
        resp.error_message = "Failed to create socket";
        return resp;
    }

    // Set connection timeout: 3 seconds
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        close(sockfd);
        freeaddrinfo(res);
        resp.error_message = "Failed to connect to " + host + ":" + std::to_string(port);
        return resp;
    }
    freeaddrinfo(res);

    std::size_t sent = 0;
    while (sent < request.size()) {
        const ssize_t count = send(sockfd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            close(sockfd);
            resp.error_message = "Failed to send HTTP request";
            return resp;
        }
        sent += static_cast<std::size_t>(count);
    }

    // Bound the complete response (headers plus body) while receiving; never
    // append an untrusted peer's stream without an allocation limit.
    std::string raw_response;
    char buffer[4096];
    std::size_t header_end = std::string::npos;
    ssize_t bytes_read = 0;
    while (true) {
        bytes_read = recv(sockfd, buffer, sizeof(buffer), 0);
        if (bytes_read == 0) break;
        if (bytes_read < 0) {
            if (errno == EINTR) continue;
            close(sockfd);
            resp.error_message = "Failed while reading HTTP response";
            return resp;
        }

        const auto received = static_cast<std::size_t>(bytes_read);
        if (received > MaxHttpResponseBytes - raw_response.size()) {
            close(sockfd);
            resp.error_message = "HTTP response exceeds 16 MiB size limit";
            return resp;
        }
        raw_response.append(buffer, received);

        header_end = raw_response.find("\r\n\r\n");
        std::size_t received_header_size = 4;
        if (header_end == std::string::npos) {
            header_end = raw_response.find("\n\n");
            received_header_size = 2;
        }
        if ((header_end == std::string::npos && raw_response.size() > MaxHttpHeaderBytes) ||
            (header_end != std::string::npos && header_end + received_header_size > MaxHttpHeaderBytes)) {
            close(sockfd);
            resp.error_message = "HTTP response headers exceed 64 KiB size limit";
            return resp;
        }
    }
    close(sockfd);

    if (raw_response.empty()) {
        resp.error_message = "Empty HTTP response";
        return resp;
    }

    // Parse HTTP response
    header_end = raw_response.find("\r\n\r\n");
    std::size_t header_separator_size = 4;
    if (header_end == std::string::npos) {
        header_end = raw_response.find("\n\n");
        header_separator_size = 2;
    }
    if (header_end == std::string::npos) {
        resp.error_message = "Malformed HTTP response: missing header terminator";
        return resp;
    }

    std::string header_part = raw_response.substr(0, header_end);
    std::string body_part = raw_response.substr(header_end + header_separator_size);

    // Status line: HTTP/1.1 200 OK
    std::stringstream hss(header_part);
    std::string status_line;
    if (std::getline(hss, status_line)) {
        std::stringstream sl(status_line);
        std::string http_ver;
        int code = 0;
        sl >> http_ver >> code;
        resp.status_code = code;
    }

    // Headers
    std::string line;
    while (std::getline(hss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string header_name = line.substr(0, colon);
            std::string header_val = line.substr(colon + 1);
            while (!header_val.empty() && (header_val.front() == ' ' || header_val.front() == '\t')) {
                header_val.erase(header_val.begin());
            }
            header_name = ascii_lower(header_name);
            resp.headers[header_name] = header_val;
            if (header_name == "content-type") {
                resp.content_type = header_val;
            }
        }
    }

    if (const auto content_length = resp.headers.find("content-length"); content_length != resp.headers.end()) {
        std::string_view value = content_length->second;
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
        std::size_t declared_size = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), declared_size);
        if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
            resp.error_message = "Malformed HTTP Content-Length";
            return resp;
        }
        if (declared_size > MaxHttpResponseBytes) {
            resp.error_message = "HTTP response exceeds 16 MiB size limit";
            return resp;
        }
    }

    resp.body = std::move(body_part);
    resp.success = (resp.status_code >= 200 && resp.status_code < 400);
    return resp;
}

} // namespace litebird
