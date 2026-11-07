#include "litebird/net/WebSocket.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/Heap.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <random>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace litebird {

// RFC 3174 SHA-1 implementation
std::string WebSocketObject::sha1_digest(const std::string& input) {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    uint64_t bit_len = static_cast<uint64_t>(input.size()) * 8;
    std::vector<uint8_t> msg(input.begin(), input.end());
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));
    }

    auto rol = [](uint32_t value, uint32_t bits) -> uint32_t {
        return (value << bits) | (value >> (32 - bits));
    };

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int t = 0; t < 16; ++t) {
            w[t] = (static_cast<uint32_t>(msg[chunk + t * 4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + t * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + t * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[chunk + t * 4 + 3]));
        }
        for (int t = 16; t < 80; ++t) {
            w[t] = rol(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
        }

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int t = 0; t < 80; ++t) {
            uint32_t f, k;
            if (t < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (t < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (t < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = rol(a, 5) + f + e + k + w[t];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    std::string digest(20, '\0');
    for (int i = 0; i < 4; ++i) {
        digest[i]      = static_cast<char>((h0 >> ((3 - i) * 8)) & 0xFF);
        digest[i + 4]  = static_cast<char>((h1 >> ((3 - i) * 8)) & 0xFF);
        digest[i + 8]  = static_cast<char>((h2 >> ((3 - i) * 8)) & 0xFF);
        digest[i + 12] = static_cast<char>((h3 >> ((3 - i) * 8)) & 0xFF);
        digest[i + 16] = static_cast<char>((h4 >> ((3 - i) * 8)) & 0xFF);
    }
    return digest;
}

std::string WebSocketObject::base64_encode(const std::string& in) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (uint8_t c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(tbl[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(tbl[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string WebSocketObject::calculate_accept_key(const std::string& client_key) {
    static const std::string guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = client_key + guid;
    std::string sha = sha1_digest(combined);
    return base64_encode(sha);
}

WebSocketObject::WebSocketObject(std::string url, std::string protocols)
    : m_url(std::move(url)), m_protocol(std::move(protocols)) {
    // Parse ws://host[:port]/path
    std::string s = m_url;
    bool is_wss = false;
    if (s.starts_with("ws://")) {
        s = s.substr(5);
        m_port = 80;
    } else if (s.starts_with("wss://")) {
        s = s.substr(6);
        m_port = 443;
        is_wss = true;
    }
    (void)is_wss;

    size_t slash_pos = s.find('/');
    std::string host_port = (slash_pos != std::string::npos) ? s.substr(0, slash_pos) : s;
    m_path = (slash_pos != std::string::npos) ? s.substr(slash_pos) : "/";

    size_t colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        m_host = host_port.substr(0, colon_pos);
        try {
            m_port = static_cast<uint16_t>(std::stoi(host_port.substr(colon_pos + 1)));
        } catch (...) {}
    } else {
        m_host = host_port;
    }
}

WebSocketObject::~WebSocketObject() {
    close(1001, "Object destroyed");
    if (m_receiver_thread.joinable()) {
        if (std::this_thread::get_id() != m_receiver_thread.get_id()) {
            m_receiver_thread.join();
        } else {
            m_receiver_thread.detach();
        }
    }
}

void WebSocketObject::connect() {
    m_ready_state = WebSocketReadyState::Connecting;
    m_receiver_thread = std::thread([this]() {
        receiver_thread_loop();
    });
}

bool WebSocketObject::read_exact(int sock, void* buf, size_t len) {
    uint8_t* ptr = static_cast<uint8_t*>(buf);
    size_t total = 0;
    while (total < len) {
        ssize_t n = ::recv(sock, ptr + total, len - total, 0);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

bool WebSocketObject::perform_handshake(int sock) {
    // Generate 16-byte random key
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 255);
    std::string raw_key(16, '\0');
    for (int i = 0; i < 16; ++i) {
        raw_key[i] = static_cast<char>(dis(gen));
    }
    std::string sec_key = base64_encode(raw_key);
    std::string expected_accept = calculate_accept_key(sec_key);

    std::ostringstream req;
    req << "GET " << m_path << " HTTP/1.1\r\n";
    req << "Host: " << m_host << ":" << m_port << "\r\n";
    req << "Upgrade: websocket\r\n";
    req << "Connection: Upgrade\r\n";
    req << "Sec-WebSocket-Key: " << sec_key << "\r\n";
    req << "Sec-WebSocket-Version: 13\r\n";
    if (!m_protocol.empty()) {
        req << "Sec-WebSocket-Protocol: " << m_protocol << "\r\n";
    }
    req << "\r\n";

    std::string req_str = req.str();
    if (::send(sock, req_str.data(), req_str.size(), 0) <= 0) {
        return false;
    }

    // Read HTTP response header until "\r\n\r\n"
    std::string resp;
    char ch;
    while (::recv(sock, &ch, 1, 0) > 0) {
        resp.push_back(ch);
        if (resp.size() >= 4 && resp.substr(resp.size() - 4) == "\r\n\r\n") {
            break;
        }
    }

    // Validate 101 Switching Protocols
    if (resp.find("101") == std::string::npos && resp.find("Switching Protocols") == std::string::npos) {
        return false;
    }

    // Validate Sec-WebSocket-Accept
    if (resp.find(expected_accept) == std::string::npos) {
        return false;
    }

    return true;
}

void WebSocketObject::receiver_thread_loop() {
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        m_ready_state = WebSocketReadyState::Closed;
        EventLoop::default_loop().post_task([weak_self = weak_from_this()]() {
            auto self = weak_self.lock();
            if (self && self->m_onerror.is_function()) {
                JsValue ev = JsValue::make_object(Heap::default_heap().allocate_object());
                ev.set_property("message", JsValue::make_string("Socket creation failed"));
                self->m_onerror.call({ev});
            }
        });
        return;
    }

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(m_port);

    if (m_host == "localhost" || m_host == "127.0.0.1") {
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    } else {
        struct hostent* he = gethostbyname(m_host.c_str());
        if (!he) {
            ::close(sock);
            m_ready_state = WebSocketReadyState::Closed;
            EventLoop::default_loop().post_task([weak_self = weak_from_this()]() {
                auto self = weak_self.lock();
                if (self && self->m_onerror.is_function()) {
                    JsValue ev = JsValue::make_object(Heap::default_heap().allocate_object());
                    ev.set_property("message", JsValue::make_string("Host resolution failed"));
                    self->m_onerror.call({ev});
                }
            });
            return;
        }
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (::connect(sock, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        ::close(sock);
        m_ready_state = WebSocketReadyState::Closed;
        EventLoop::default_loop().post_task([weak_self = weak_from_this()]() {
            auto self = weak_self.lock();
            if (self && self->m_onerror.is_function()) {
                JsValue ev = JsValue::make_object(Heap::default_heap().allocate_object());
                ev.set_property("message", JsValue::make_string("Connection failed"));
                self->m_onerror.call({ev});
            }
        });
        return;
    }

    if (!perform_handshake(sock)) {
        ::close(sock);
        m_ready_state = WebSocketReadyState::Closed;
        EventLoop::default_loop().post_task([weak_self = weak_from_this()]() {
            auto self = weak_self.lock();
            if (self && self->m_onerror.is_function()) {
                JsValue ev = JsValue::make_object(Heap::default_heap().allocate_object());
                ev.set_property("message", JsValue::make_string("WebSocket handshake failed"));
                self->m_onerror.call({ev});
            }
        });
        return;
    }

    m_socket_fd = sock;
    m_ready_state = WebSocketReadyState::Open;

    EventLoop::default_loop().post_task([weak_self = weak_from_this()]() {
        auto self = weak_self.lock();
        if (!self) return;
        JsValue ev = JsValue::make_object(Heap::default_heap().allocate_object());
        ev.set_property("type", JsValue::make_string("open"));
        if (self->m_onopen.is_function()) {
            self->m_onopen.call({ev});
        }
        for (auto& l : self->m_open_listeners) {
            if (l.is_function()) l.call({ev});
        }
    });

    // Frame reader loop
    while (m_ready_state == WebSocketReadyState::Open && !m_closed) {
        uint8_t header[2];
        if (!read_exact(sock, header, 2)) break;

        bool fin = (header[0] & 0x80) != 0;
        uint8_t opcode = header[0] & 0x0F;
        bool masked = (header[1] & 0x80) != 0;
        uint64_t payload_len = header[1] & 0x7F;
        (void)fin;

        if (payload_len == 126) {
            uint16_t len16;
            if (!read_exact(sock, &len16, 2)) break;
            payload_len = ntohs(len16);
        } else if (payload_len == 127) {
            uint64_t len64;
            if (!read_exact(sock, &len64, 8)) break;
            payload_len = be64toh(len64);
        }

        uint8_t mask_key[4] = {0};
        if (masked) {
            if (!read_exact(sock, mask_key, 4)) break;
        }

        std::vector<uint8_t> payload(payload_len);
        if (payload_len > 0) {
            if (!read_exact(sock, payload.data(), payload_len)) break;
            if (masked) {
                for (size_t i = 0; i < payload_len; ++i) {
                    payload[i] ^= mask_key[i % 4];
                }
            }
        }

        if (opcode == 0x1) {
            // Text frame
            std::string text(payload.begin(), payload.end());
            EventLoop::default_loop().post_task([weak_self = weak_from_this(), msg = std::move(text)]() {
                auto self = weak_self.lock();
                if (!self) return;
                JsValue ev = JsValue::make_object(Heap::default_heap().allocate_object());
                ev.set_property("data", JsValue::make_string(msg));
                ev.set_property("type", JsValue::make_string("message"));

                if (self->m_onmessage.is_function()) {
                    self->m_onmessage.call({ev});
                }
                for (auto& l : self->m_message_listeners) {
                    if (l.is_function()) l.call({ev});
                }
            });
        } else if (opcode == 0x8) {
            // Close frame
            int code = 1000;
            std::string reason;
            if (payload_len >= 2) {
                code = (payload[0] << 8) | payload[1];
                if (payload_len > 2) {
                    reason = std::string(payload.begin() + 2, payload.end());
                }
            }
            m_close_code = code;
            m_close_reason = reason;
            break;
        } else if (opcode == 0x9) {
            // Ping -> Pong response (opcode 0xA)
            std::lock_guard<std::mutex> lock(m_send_mutex);
            std::vector<uint8_t> pong_frame;
            pong_frame.push_back(0x8A); // FIN + Pong
            pong_frame.push_back(0x80 | (payload_len & 0x7F)); // Masked
            uint8_t pmask[4] = {1, 2, 3, 4};
            pong_frame.insert(pong_frame.end(), pmask, pmask + 4);
            for (size_t i = 0; i < payload_len; ++i) {
                pong_frame.push_back(payload[i] ^ pmask[i % 4]);
            }
            ::send(sock, pong_frame.data(), pong_frame.size(), 0);
        }
    }

    if (m_socket_fd >= 0) {
        ::close(m_socket_fd);
        m_socket_fd = -1;
    }
    m_ready_state = WebSocketReadyState::Closed;

    EventLoop::default_loop().post_task([weak_self = weak_from_this()]() {
        auto self = weak_self.lock();
        if (!self) return;
        JsValue ev = JsValue::make_object(Heap::default_heap().allocate_object());
        ev.set_property("type", JsValue::make_string("close"));
        ev.set_property("code", JsValue::make_number(self->m_close_code));
        ev.set_property("reason", JsValue::make_string(self->m_close_reason));
        ev.set_property("wasClean", JsValue::make_boolean(true));

        if (self->m_onclose.is_function()) {
            self->m_onclose.call({ev});
        }
        for (auto& l : self->m_close_listeners) {
            if (l.is_function()) l.call({ev});
        }
    });
}

void WebSocketObject::send(const std::string& message) {
    if (m_ready_state != WebSocketReadyState::Open || m_socket_fd < 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_send_mutex);
    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN = 1, Opcode = 1 (Text)

    size_t len = message.size();
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(0x80 | len));
    } else if (len <= 65535) {
        frame.push_back(static_cast<uint8_t>(0x80 | 126));
        uint16_t len16 = htons(static_cast<uint16_t>(len));
        uint8_t* p = reinterpret_cast<uint8_t*>(&len16);
        frame.push_back(p[0]);
        frame.push_back(p[1]);
    } else {
        frame.push_back(static_cast<uint8_t>(0x80 | 127));
        uint64_t len64 = htobe64(len);
        uint8_t* p = reinterpret_cast<uint8_t*>(&len64);
        for (int i = 0; i < 8; ++i) frame.push_back(p[i]);
    }

    // 4-byte random mask
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 255);
    uint8_t mask[4];
    for (int i = 0; i < 4; ++i) {
        mask[i] = static_cast<uint8_t>(dis(gen));
        frame.push_back(mask[i]);
    }

    for (size_t i = 0; i < len; ++i) {
        frame.push_back(static_cast<uint8_t>(message[i]) ^ mask[i % 4]);
    }

    ::send(m_socket_fd, frame.data(), frame.size(), 0);
}

void WebSocketObject::close(int code, const std::string& reason) {
    if (m_ready_state == WebSocketReadyState::Closing || m_ready_state == WebSocketReadyState::Closed) {
        return;
    }

    m_ready_state = WebSocketReadyState::Closing;
    m_closed = true;

    if (m_socket_fd >= 0) {
        std::lock_guard<std::mutex> lock(m_send_mutex);
        std::vector<uint8_t> frame;
        frame.push_back(0x88); // FIN + Close opcode
        size_t payload_len = 2 + reason.size();
        frame.push_back(static_cast<uint8_t>(0x80 | payload_len));

        uint8_t mask[4] = {0xAA, 0xBB, 0xCC, 0xDD};
        for (int i = 0; i < 4; ++i) frame.push_back(mask[i]);

        uint16_t code16 = htons(static_cast<uint16_t>(code));
        uint8_t* cp = reinterpret_cast<uint8_t*>(&code16);
        frame.push_back(cp[0] ^ mask[0]);
        frame.push_back(cp[1] ^ mask[1]);

        for (size_t i = 0; i < reason.size(); ++i) {
            frame.push_back(static_cast<uint8_t>(reason[i]) ^ mask[(2 + i) % 4]);
        }

        ::send(m_socket_fd, frame.data(), frame.size(), 0);
        ::shutdown(m_socket_fd, SHUT_RDWR);
    }
}

void WebSocketObject::add_event_listener(const std::string& type, JsValue listener) {
    if (type == "open") m_open_listeners.push_back(listener);
    else if (type == "message") m_message_listeners.push_back(listener);
    else if (type == "error") m_error_listeners.push_back(listener);
    else if (type == "close") m_close_listeners.push_back(listener);
}

// -------------------------------------------------------------
// Loopback Server for Offline Testing
// -------------------------------------------------------------

WebSocketObject::LoopbackServer::~LoopbackServer() {
    running = false;
    if (listen_fd >= 0) {
        ::close(listen_fd);
        listen_fd = -1;
    }
    if (thread.joinable()) {
        thread.join();
    }
}

std::shared_ptr<WebSocketObject::LoopbackServer> WebSocketObject::start_loopback_server(std::function<void(int client_fd)> on_client) {
    auto server = std::make_shared<LoopbackServer>();
    server->listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = 0; // ephemeral port

    bind(server->listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    listen(server->listen_fd, 5);

    socklen_t len = sizeof(addr);
    getsockname(server->listen_fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
    server->port = ntohs(addr.sin_port);

    server->thread = std::thread([server, on_client]() {
        while (server->running) {
            struct sockaddr_in client_addr{};
            socklen_t clen = sizeof(client_addr);
            int client = accept(server->listen_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &clen);
            if (client < 0) break;
            on_client(client);
        }
    });

    return server;
}

// -------------------------------------------------------------
// JavaScript Factory & Constructor Bindings
// -------------------------------------------------------------

JsValue WebSocketObject::create_js_instance(std::string_view url, std::string_view protocols) {
    auto ws = std::make_shared<WebSocketObject>(std::string(url), std::string(protocols));
    ws->connect();

    JsValue inst = JsValue::make_object(Heap::default_heap().allocate_object());

    inst.set_property("url", JsValue::make_string(ws->url()));
    inst.set_property("protocol", JsValue::make_string(ws->protocol()));
    inst.set_property("bufferedAmount", JsValue::make_number(0));

    inst.set_property("send", JsValue::make_native_function([ws](const std::vector<JsValue>& args) {
        if (!args.empty()) {
            ws->send(args[0].to_string());
        }
        return JsValue::make_undefined();
    }));

    inst.set_property("close", JsValue::make_native_function([ws](const std::vector<JsValue>& args) {
        int code = (args.size() > 0 && args[0].type == JsType::Number) ? static_cast<int>(args[0].number_val) : 1000;
        std::string reason = (args.size() > 1) ? args[1].to_string() : "";
        ws->close(code, reason);
        return JsValue::make_undefined();
    }));

    inst.set_property("addEventListener", JsValue::make_native_function([ws](const std::vector<JsValue>& args) {
        if (args.size() >= 2 && args[1].is_function()) {
            ws->add_event_listener(args[0].to_string(), args[1]);
        }
        return JsValue::make_undefined();
    }));

    // Synchronize event callbacks dynamically with inst properties
    ws->set_onopen(JsValue::make_native_function([inst](const std::vector<JsValue>& ev) {
        JsValue h = inst.get_property("onopen");
        if (h.is_function()) h.call(ev);
        return JsValue::make_undefined();
    }));

    ws->set_onmessage(JsValue::make_native_function([inst](const std::vector<JsValue>& ev) {
        JsValue h = inst.get_property("onmessage");
        if (h.is_function()) h.call(ev);
        return JsValue::make_undefined();
    }));

    ws->set_onerror(JsValue::make_native_function([inst](const std::vector<JsValue>& ev) {
        JsValue h = inst.get_property("onerror");
        if (h.is_function()) h.call(ev);
        return JsValue::make_undefined();
    }));

    ws->set_onclose(JsValue::make_native_function([inst](const std::vector<JsValue>& ev) {
        JsValue h = inst.get_property("onclose");
        if (h.is_function()) h.call(ev);
        return JsValue::make_undefined();
    }));

    // Dynamic readyState getter
    inst.set_property("__get_readyState__", JsValue::make_native_function([ws](const std::vector<JsValue>&) {
        return JsValue::make_number(static_cast<double>(ws->ready_state()));
    }));

    return inst;
}

JsValue WebSocketObject::create_constructor() {
    JsValue ctor = JsValue::make_native_function([](const std::vector<JsValue>& args) {
        if (args.empty()) {
            throw std::runtime_error("TypeError: Failed to construct 'WebSocket': 1 argument required");
        }
        std::string url = args[0].to_string();
        std::string proto = (args.size() > 1) ? args[1].to_string() : "";
        return WebSocketObject::create_js_instance(url, proto);
    });

    // Standard WebSocket readyState constants
    ctor.set_property("CONNECTING", JsValue::make_number(0));
    ctor.set_property("OPEN", JsValue::make_number(1));
    ctor.set_property("CLOSING", JsValue::make_number(2));
    ctor.set_property("CLOSED", JsValue::make_number(3));

    return ctor;
}

} // namespace litebird
