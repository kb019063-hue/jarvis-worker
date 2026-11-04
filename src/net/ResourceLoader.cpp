#include "litebird/net/ResourceLoader.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <vector>
#include <algorithm>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

namespace litebird {

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
        if (base_url.rfind("http://", 0) == 0) {
            // Find end of host:port
            auto slash_pos = base_url.find('/', 7);
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
    std::string prefix;
    if (is_http) {
        prefix = "http://";
        std::string without_scheme = combined.substr(7);
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

ResourceResponse ResourceLoader::load(const std::string& url) {
    if (url.rfind("http://", 0) == 0) {
        return load_http(url);
    }

    std::string path = url;
    if (path.rfind("file://", 0) == 0) {
        path = path.substr(7);
    }
    return load_file(path);
}

ResourceResponse ResourceLoader::load_file(const std::string& file_path) {
    ResourceResponse resp;
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
    return resp;
}

ResourceResponse ResourceLoader::load_http(const std::string& url) {
    ResourceResponse resp;
    // Expected format: http://host[:port][/path]
    if (url.rfind("http://", 0) != 0) {
        resp.error_message = "Unsupported protocol: only http:// is supported";
        return resp;
    }

    std::string without_scheme = url.substr(7);
    auto slash_pos = without_scheme.find('/');
    std::string host_port = (slash_pos == std::string::npos) ? without_scheme : without_scheme.substr(0, slash_pos);
    std::string path = (slash_pos == std::string::npos) ? "/" : without_scheme.substr(slash_pos);

    std::string host = host_port;
    int port = 80;
    auto colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        host = host_port.substr(0, colon_pos);
        try {
            port = std::stoi(host_port.substr(colon_pos + 1));
        } catch (...) {
            port = 80;
        }
    }

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

    // Send HTTP GET request
    std::string request = "GET " + path + " HTTP/1.1\r\n" +
                          "Host: " + host + "\r\n" +
                          "User-Agent: Litebird/1.0 (Native Engine)\r\n" +
                          "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n" +
                          "Connection: close\r\n\r\n";

    if (send(sockfd, request.c_str(), request.size(), 0) < 0) {
        close(sockfd);
        resp.error_message = "Failed to send HTTP request";
        return resp;
    }

    // Read full response
    std::string raw_response;
    char buffer[4096];
    ssize_t bytes_read = 0;
    while ((bytes_read = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        raw_response.append(buffer, static_cast<size_t>(bytes_read));
    }
    close(sockfd);

    if (raw_response.empty()) {
        resp.error_message = "Empty HTTP response";
        return resp;
    }

    // Parse HTTP response
    auto header_end = raw_response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        header_end = raw_response.find("\n\n");
    }

    std::string header_part = (header_end != std::string::npos) ? raw_response.substr(0, header_end) : raw_response;
    std::string body_part = (header_end != std::string::npos) ?
        raw_response.substr(header_end + (raw_response[header_end] == '\r' ? 4 : 2)) : "";

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
            std::transform(header_name.begin(), header_name.end(), header_name.begin(), ::tolower);
            if (header_name == "content-type") {
                resp.content_type = header_val;
            }
        }
    }

    resp.body = std::move(body_part);
    resp.success = (resp.status_code >= 200 && resp.status_code < 400);
    return resp;
}

} // namespace litebird
