// Phase 492, finding 492: HTTP/HTTPS and WebSocket proxy routing, RFC 7616
// Digest 407 authentication (stale=true, Proxy-Authentication-Info nextnonce,
// exact method/request-target/authority), plain ws:// CONNECT tunneling, and
// fail-closed proxy/origin isolation.
TEST(net_http_https_websocket_proxy_routing_and_digest_407) {
    using namespace litebird;

    struct EnvGuard {
        std::vector<std::pair<std::string, std::string>> prev;
        std::vector<std::string> absent;
        EnvGuard() {
            for (const char* k : {"HTTP_PROXY", "http_proxy", "HTTPS_PROXY", "https_proxy",
                                  "ALL_PROXY", "all_proxy", "NO_PROXY", "no_proxy"}) {
                if (const char* v = std::getenv(k)) prev.emplace_back(k, v);
                else absent.emplace_back(k);
                ::unsetenv(k);
            }
        }
        ~EnvGuard() {
            for (const auto& [k, v] : prev) ::setenv(k.c_str(), v.c_str(), 1);
            for (const std::string& k : absent) ::unsetenv(k.c_str());
        }
    } env_guard;

    struct StateGuard {
        ~StateGuard() {
            ResourceLoader::clear_auth_credentials();
            ResourceLoader::clear_proxy_credentials();
            ResourceLoader::clear_http_cache();
            ResourceLoader::clear_http_connection_pool();
            ResourceLoader::set_ca_bundle_for_testing("");
            WebSocketObject::set_ca_bundle_for_testing("");
        }
    } state_guard;

    auto extract_digest_param = [](std::string_view header_val, std::string_view key) -> std::string {
        const std::string needle = std::string(key) + "=";
        std::size_t pos = header_val.find(needle);
        while (pos != std::string_view::npos) {
            if (pos == 0 || header_val[pos - 1] == ' ' || header_val[pos - 1] == ',') {
                std::size_t vs = pos + needle.size();
                if (vs < header_val.size() && header_val[vs] == '"') {
                    ++vs;
                    std::size_t ve = header_val.find('"', vs);
                    if (ve != std::string_view::npos) return std::string(header_val.substr(vs, ve - vs));
                } else {
                    std::size_t ve = header_val.find_first_of(", \r\n", vs);
                    return std::string(header_val.substr(vs, ve == std::string_view::npos ? std::string_view::npos : ve - vs));
                }
            }
            pos = header_val.find(needle, pos + needle.size());
        }
        return {};
    };

    auto verify_digest = [&](const std::string& auth_hdr,
                             const std::string& method,
                             const std::string& uri,
                             const std::string& nonce,
                             const std::string& realm,
                             const std::string& user,
                             const std::string& pass) -> bool {
        if (auth_hdr.rfind("Digest ", 0) != 0) return false;
        const std::string got_user = extract_digest_param(auth_hdr, "username");
        const std::string got_realm = extract_digest_param(auth_hdr, "realm");
        const std::string got_nonce = extract_digest_param(auth_hdr, "nonce");
        const std::string got_uri = extract_digest_param(auth_hdr, "uri");
        const std::string got_nc = extract_digest_param(auth_hdr, "nc");
        const std::string got_cnonce = extract_digest_param(auth_hdr, "cnonce");
        const std::string got_resp = extract_digest_param(auth_hdr, "response");
        if (got_user != user || got_realm != realm || got_nonce != nonce || got_uri != uri) {
            return false;
        }
        std::string expected_hex;
        if (!ResourceLoader::auth_Digest_response("SHA-256", user, pass, realm, nonce,
                                                  method, uri, "auth", got_cnonce, got_nc,
                                                  expected_hex)) {
            return false;
        }
        return got_resp == expected_hex;
    };

    auto extract_header_ci = [](const std::string& raw, std::string_view key_lower) -> std::string {
        std::size_t pos = raw.find("\r\n");
        if (pos == std::string::npos) return {};
        pos += 2;
        while (pos < raw.size()) {
            std::size_t next = raw.find("\r\n", pos);
            std::string_view line = (next == std::string::npos)
                ? std::string_view(raw).substr(pos)
                : std::string_view(raw).substr(pos, next - pos);
            if (line.empty()) break;
            std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                std::string k(line.substr(0, colon));
                for (char& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (k == key_lower) {
                    std::string_view v = line.substr(colon + 1);
                    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
                    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
                    return std::string(v);
                }
            }
            if (next == std::string::npos) break;
            pos = next + 2;
        }
        return {};
    };

    auto make_listener = [](uint16_t& port_out) -> int {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(fd >= 0);
        int opt = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        assert(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        assert(::listen(fd, 32) == 0);
        socklen_t len = sizeof(addr);
        assert(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
        port_out = ntohs(addr.sin_port);
        return fd;
    };

    auto read_http_head = [](int fd, std::string& raw_out) -> bool {
        raw_out.clear();
        char ch = 0;
        while (raw_out.size() < 32768) {
            const ssize_t n = ::recv(fd, &ch, 1, 0);
            if (n <= 0) return false;
            raw_out.push_back(ch);
            if (raw_out.size() >= 4 && raw_out.compare(raw_out.size() - 4, 4, "\r\n\r\n") == 0) {
                return true;
            }
        }
        return false;
    };

    auto send_all_fd = [](int fd, std::string_view data) {
        std::size_t sent = 0;
        while (sent < data.size()) {
            const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            sent += static_cast<std::size_t>(n);
        }
    };

    auto relay_pair = [](int a, int b) {
        pollfd pfds[2]{{a, POLLIN, 0}, {b, POLLIN, 0}};
        char buf[8192];
        while (::poll(pfds, 2, 2500) > 0) {
            bool moved = false;
            for (int i = 0; i < 2; ++i) {
                if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    if (!(pfds[i].revents & POLLIN)) return;
                }
                if (pfds[i].revents & POLLIN) {
                    const ssize_t n = ::recv(pfds[i].fd, buf, sizeof(buf), 0);
                    if (n <= 0) return;
                    const int dst = pfds[1 - i].fd;
                    std::size_t s = 0;
                    while (s < static_cast<std::size_t>(n)) {
                        const ssize_t w = ::send(dst, buf + s, static_cast<std::size_t>(n) - s, MSG_NOSIGNAL);
                        if (w <= 0) return;
                        s += static_cast<std::size_t>(w);
                    }
                    moved = true;
                }
            }
            if (!moved) break;
        }
    };

    auto sha1_accept = [](std::string_view sec_key) -> std::string {
        const std::string concat = std::string(sec_key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        uint32_t h[5]{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
        std::vector<uint8_t> msg(concat.begin(), concat.end());
        const uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8ULL;
        msg.push_back(0x80);
        while ((msg.size() % 64) != 56) msg.push_back(0);
        for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xff));
        auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
        for (std::size_t off = 0; off < msg.size(); off += 64) {
            uint32_t w[80]{};
            for (int i = 0; i < 16; ++i) {
                w[i] = (uint32_t(msg[off + i * 4]) << 24) | (uint32_t(msg[off + i * 4 + 1]) << 16) |
                       (uint32_t(msg[off + i * 4 + 2]) << 8) | uint32_t(msg[off + i * 4 + 3]);
            }
            for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
            for (int i = 0; i < 80; ++i) {
                uint32_t f = 0, k = 0;
                if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
                else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
                else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
                const uint32_t temp = rol(a, 5) + f + e + k + w[i];
                e = d; d = c; c = rol(b, 30); b = a; a = temp;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
        }
        std::string raw(20, '\0');
        for (int i = 0; i < 5; ++i) {
            raw[i * 4] = static_cast<char>((h[i] >> 24) & 0xff);
            raw[i * 4 + 1] = static_cast<char>((h[i] >> 16) & 0xff);
            raw[i * 4 + 2] = static_cast<char>((h[i] >> 8) & 0xff);
            raw[i * 4 + 3] = static_cast<char>(h[i] & 0xff);
        }
        return ResourceLoader::auth_base64(raw);
    };

    auto serve_ws_echo = [&](int fd, const std::string& expected_msg, const std::string& reply_prefix) {
        std::string req;
        if (!read_http_head(fd, req)) return;
        const std::string key = extract_header_ci(req, "sec-websocket-key");
        const std::string upgrade =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + sha1_accept(key) + "\r\n\r\n";
        send_all_fd(fd, upgrade);
        uint8_t hdr[2]{};
        if (::recv(fd, hdr, 2, MSG_WAITALL) != 2) return;
        const bool masked = (hdr[1] & 0x80) != 0;
        const std::size_t plen = hdr[1] & 0x7f;
        uint8_t mask[4]{};
        if (masked && ::recv(fd, mask, 4, MSG_WAITALL) != 4) return;
        std::string payload(plen, '\0');
        if (plen > 0 && ::recv(fd, payload.data(), plen, MSG_WAITALL) != static_cast<ssize_t>(plen)) return;
        if (masked) {
            for (std::size_t i = 0; i < plen; ++i) payload[i] ^= static_cast<char>(mask[i % 4]);
        }
        assert(payload == expected_msg);
        const std::string out = reply_prefix + payload;
        std::string frame;
        frame.push_back(static_cast<char>(0x81));
        frame.push_back(static_cast<char>(out.size()));
        frame += out;
        send_all_fd(fd, frame);
        uint8_t close_in[6]{};
        if (::recv(fd, close_in, 2, MSG_WAITALL) == 2 && (close_in[1] & 0x7f) <= 125) {
            const std::size_t clen = close_in[1] & 0x7f;
            std::vector<uint8_t> junk(clen + ((close_in[1] & 0x80) ? 4 : 0));
            if (!junk.empty()) (void)::recv(fd, junk.data(), junk.size(), MSG_WAITALL);
            const char close_out[4]{static_cast<char>(0x88), 2, 0x03, static_cast<char>(0xe8)};
            send_all_fd(fd, std::string_view(close_out, 4));
        }
    };

    // -----------------------------------------------------------------------
    // 1. HTTP_PROXY absolute-form + Digest 407 + stale=true + nextnonce + CORS
    //    + redirect Proxy-Authorization non-forwarding + isolation
    // -----------------------------------------------------------------------
    {
        uint16_t proxy_port = 0;
        const int listener = make_listener(proxy_port);
        const std::string proxy_auth = "127.0.0.1:" + std::to_string(proxy_port);
        const std::string proxy_url = "http://" + proxy_auth;

        struct HttpProxyLog {
            std::vector<std::string> req_lines;
            bool saw_origin_auth{false};
            bool redirect_leaked_proxy_auth{false};
        } log;

        std::thread srv([&]() {
            // Sequence:
            //   0: GET http://target.test:8080/item?a=1 (no auth -> 407 n1)
            //   1: GET http://target.test:8080/item?a=1 (auth n1 -> 407 stale=true n2)
            //   2: GET http://target.test:8080/item?a=1 (auth n2 -> 200 + nextnonce="n3")
            //   3: GET http://target.test:8080/second (no auth -> 407 stale=false nonce="n2", expects nextnonce="n3")
            //   4: GET http://target.test:8080/second (auth n3 -> 200)
            //   5: OPTIONS http://cors.test:8080/api (CORS preflight no auth -> 407 n4)
            //   6: OPTIONS http://cors.test:8080/api (auth n3 nc=2 -> 200 CORS)
            //   7: GET http://cors.test:8080/api (no auth -> 407 n5)
            //   8: GET http://cors.test:8080/api (auth n3 nc=3 -> 200 CORS)
            //   9: GET http://redir.test:8080/start (no auth -> 407 n6)
            //  10: GET http://redir.test:8080/start (auth n3 nc=4 -> 302 Location: http://other.test:8080/landed)
            //  11: GET http://other.test:8080/landed (must NOT send Proxy-Authorization!)
            for (int step = 0; step < 12; ++step) {
                pollfd pfd{listener, POLLIN, 0};
                if (::poll(&pfd, 1, 3000) <= 0) break;
                const int cfd = ::accept(listener, nullptr, nullptr);
                if (cfd < 0) break;
                timeval tv{2, 0};
                ::setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                std::string raw;
                if (!read_http_head(cfd, raw)) { ::close(cfd); break; }
                const std::size_t eol = raw.find("\r\n");
                log.req_lines.push_back(raw.substr(0, eol));
                const std::string pa = extract_header_ci(raw, "proxy-authorization");
                const std::string oa = extract_header_ci(raw, "authorization");
                if (!oa.empty()) log.saw_origin_auth = true;

                std::string reply;
                if (step == 0) {
                    assert(pa.empty());
                    assert(raw.rfind("GET http://target.test:8080/item?a=1 HTTP/1.1\r\n", 0) == 0);
                    reply = "HTTP/1.1 407 Proxy Authentication Required\r\n"
                            "Proxy-Authenticate: Digest realm=\"gw\", algorithm=SHA-256, qop=\"auth\", nonce=\"n1\"\r\n"
                            "Set-Cookie: spoof=1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                } else if (step == 1) {
                    assert(verify_digest(pa, "GET", "http://target.test:8080/item?a=1", "n1", "gw", "puser", "ppass"));
                    reply = "HTTP/1.1 407 Proxy Authentication Required\r\n"
                            "Proxy-Authenticate: Digest realm=\"gw\", algorithm=SHA-256, qop=\"auth\", nonce=\"n2\", stale=true\r\n"
                            "Content-Length: 0\r\nConnection: close\r\n\r\n";
                } else if (step == 2) {
                    assert(verify_digest(pa, "GET", "http://target.test:8080/item?a=1", "n2", "gw", "puser", "ppass"));
                    reply = "HTTP/1.1 200 OK\r\n"
                            "Proxy-Authentication-Info: nextnonce=\"n3\"\r\n"
                            "Content-Length: 10\r\nConnection: close\r\n\r\nproxied-ok";
                } else if (step == 3) {
                    assert(pa.empty());
                    reply = "HTTP/1.1 407 Proxy Authentication Required\r\n"
                            "Proxy-Authenticate: Digest realm=\"gw\", algorithm=SHA-256, qop=\"auth\", nonce=\"n2\"\r\n"
                            "Content-Length: 0\r\nConnection: close\r\n\r\n";
                } else if (step == 4) {
                    assert(verify_digest(pa, "GET", "http://target.test:8080/second", "n3", "gw", "puser", "ppass"));
                    reply = "HTTP/1.1 200 OK\r\nContent-Length: 9\r\nConnection: close\r\n\r\nsecond-ok";
                } else if (step == 5) {
                    assert(raw.rfind("OPTIONS http://cors.test:8080/api HTTP/1.1\r\n", 0) == 0);
                    assert(pa.empty());
                    reply = "HTTP/1.1 407 Proxy Authentication Required\r\n"
                            "Proxy-Authenticate: Digest realm=\"gw\", algorithm=SHA-256, qop=\"auth\", nonce=\"n4\"\r\n"
                            "Content-Length: 0\r\nConnection: close\r\n\r\n";
                } else if (step == 6) {
                    assert(verify_digest(pa, "OPTIONS", "http://cors.test:8080/api", "n3", "gw", "puser", "ppass"));
                    assert(extract_digest_param(pa, "nc") == "00000002");
                    reply = "HTTP/1.1 200 OK\r\n"
                            "Access-Control-Allow-Origin: http://app.test\r\n"
                            "Access-Control-Allow-Methods: GET\r\n"
                            "Access-Control-Allow-Headers: X-Custom\r\n"
                            "Content-Length: 0\r\nConnection: close\r\n\r\n";
                } else if (step == 7) {
                    assert(raw.rfind("GET http://cors.test:8080/api HTTP/1.1\r\n", 0) == 0);
                    assert(pa.empty());
                    reply = "HTTP/1.1 407 Proxy Authentication Required\r\n"
                            "Proxy-Authenticate: Digest realm=\"gw\", algorithm=SHA-256, qop=\"auth\", nonce=\"n5\"\r\n"
                            "Content-Length: 0\r\nConnection: close\r\n\r\n";
                } else if (step == 8) {
                    assert(verify_digest(pa, "GET", "http://cors.test:8080/api", "n3", "gw", "puser", "ppass"));
                    assert(extract_digest_param(pa, "nc") == "00000003");
                    reply = "HTTP/1.1 200 OK\r\n"
                            "Access-Control-Allow-Origin: http://app.test\r\n"
                            "Content-Length: 7\r\nConnection: close\r\n\r\ncors-ok";
                } else if (step == 9) {
                    assert(raw.rfind("GET http://redir.test:8080/start HTTP/1.1\r\n", 0) == 0);
                    assert(pa.empty());
                    reply = "HTTP/1.1 407 Proxy Authentication Required\r\n"
                            "Proxy-Authenticate: Digest realm=\"gw\", algorithm=SHA-256, qop=\"auth\", nonce=\"n6\"\r\n"
                            "Content-Length: 0\r\nConnection: close\r\n\r\n";
                } else if (step == 10) {
                    assert(verify_digest(pa, "GET", "http://redir.test:8080/start", "n3", "gw", "puser", "ppass"));
                    assert(extract_digest_param(pa, "nc") == "00000004");
                    reply = "HTTP/1.1 302 Found\r\n"
                            "Location: http://other.test:8080/landed\r\n"
                            "Access-Control-Allow-Origin: http://app.test\r\n"
                            "Content-Length: 0\r\nConnection: close\r\n\r\n";
                } else {
                    if (!pa.empty()) log.redirect_leaked_proxy_auth = true;
                    reply = "HTTP/1.1 200 OK\r\n"
                            "Access-Control-Allow-Origin: *\r\n"
                            "Content-Length: 6\r\nConnection: close\r\n\r\nlanded";
                }
                send_all_fd(cfd, reply);
                ::close(cfd);
            }
            ::close(listener);
        });

        ::setenv("HTTP_PROXY", proxy_url.c_str(), 1);
        ResourceLoader::clear_auth_credentials();
        ResourceLoader::clear_proxy_credentials();
        ResourceLoader::clear_http_cache();
        ResourceLoader::clear_http_connection_pool();
        // Set conflicting origin credentials; they must never reach the proxy.
        ResourceLoader::set_auth_credentials("http://" + proxy_auth, "gw", "docuser", "docpass");
        ResourceLoader::set_proxy_credentials(proxy_auth, "gw", "puser", "ppass");

        ResourceRequestContext ctx;
        ctx.document_url = "http://app.test/index.html";
        ctx.mode = RequestMode::Cors;

        // First request: 407 (n1) -> 407 stale=true (n2) -> 200 + nextnonce="n3".
        // Also verifies Set-Cookie on 407 was ignored.
        const ResourceResponse r1 = ResourceLoader::load_http("http://target.test:8080/item?a=1", "GET", {}, "");
        assert(r1.network_ok && r1.status_code == 200 && r1.body == "proxied-ok");
        assert(CookieJar::default_jar().get_cookie_header("http://target.test:8080/item", "http://target.test:8080/", true, "GET").empty());

        // Clearing document credentials leaves BOTH proxy credentials AND proxy nextnonce="n3" intact.
        ResourceLoader::clear_auth_credentials();
        assert(ResourceLoader::proxy_credential_count() == 1);

        const ResourceResponse r2 = ResourceLoader::load_http("http://target.test:8080/second", "GET", {}, "");
        assert(r2.network_ok && r2.status_code == 200 && r2.body == "second-ok");

        // Fetch/XHR CORS preflight + GET through proxy 407
        const ResourceResponse r3 = ResourceLoader::load_for_page(
            "http://cors.test:8080/api", ctx, "GET", {{"X-Custom", "1"}}, "");
        assert(r3.network_ok && r3.status_code == 200 && r3.body == "cors-ok");

        // Redirect hop must not receive preemptive Proxy-Authorization
        const ResourceResponse r4 = ResourceLoader::load_for_page(
            "http://redir.test:8080/start", ctx, "GET", {}, "");
        assert(r4.network_ok && r4.status_code == 200 && r4.body == "landed");

        srv.join();
        assert(!log.saw_origin_auth);
        assert(!log.redirect_leaked_proxy_auth);
        assert(log.req_lines.size() == 12);

        // NO_PROXY bypasses HTTP_PROXY cleanly (reserved .test fails directly without touching proxy).
        ::setenv("NO_PROXY", ".target.test:8080", 1);
        const ResourceResponse bypassed = ResourceLoader::load_http("http://sub.target.test:8080/item", "GET", {}, "");
        assert(!bypassed.network_ok);
        assert(bypassed.error_message.find("Reserved non-existent domain") != std::string::npos);
        ::unsetenv("NO_PROXY");

        // Invalid HTTP_PROXY fails closed (never falls back to direct).
        ::setenv("HTTP_PROXY", "http://user:pass@127.0.0.1:9999", 1);
        const ResourceResponse invalid_p = ResourceLoader::load_http("http://127.0.0.1:1/x", "GET", {}, "");
        assert(!invalid_p.network_ok && invalid_p.error_message == "Invalid HTTP proxy URL");
        ::unsetenv("HTTP_PROXY");
    }

    // -----------------------------------------------------------------------
    // 2. HTTPS_PROXY CONNECT tunnel + Digest 407 + stale=true + TLS SNI +
    //    Proxy-Authorization stripped from inner TLS + fail-closed 407
    // -----------------------------------------------------------------------
    {
        void* ssl_lib = ::dlopen("libssl.so.3", RTLD_LAZY | RTLD_LOCAL);
        assert(ssl_lib != nullptr);
        auto load_sym = [&](auto& fn, const char* sym) {
            void* p = ::dlsym(ssl_lib, sym);
            assert(p != nullptr);
            std::memcpy(&fn, &p, sizeof(p));
        };
        const void* (*tls_server_method_fn)() = nullptr;
        void* (*ctx_new_fn)(const void*) = nullptr;
        void (*ctx_free_fn)(void*) = nullptr;
        int (*ctx_use_cert_fn)(void*, const char*, int) = nullptr;
        int (*ctx_use_key_fn)(void*, const char*, int) = nullptr;
        void* (*ssl_new_fn)(void*) = nullptr;
        void (*ssl_free_fn)(void*) = nullptr;
        int (*ssl_set_fd_fn)(void*, int) = nullptr;
        int (*ssl_accept_fn)(void*) = nullptr;
        int (*ssl_read_fn)(void*, void*, int) = nullptr;
        int (*ssl_write_fn)(void*, const void*, int) = nullptr;
        int (*ssl_shutdown_fn)(void*) = nullptr;
        load_sym(tls_server_method_fn, "TLS_server_method");
        load_sym(ctx_new_fn, "SSL_CTX_new");
        load_sym(ctx_free_fn, "SSL_CTX_free");
        load_sym(ctx_use_cert_fn, "SSL_CTX_use_certificate_file");
        load_sym(ctx_use_key_fn, "SSL_CTX_use_PrivateKey_file");
        load_sym(ssl_new_fn, "SSL_new");
        load_sym(ssl_free_fn, "SSL_free");
        load_sym(ssl_set_fd_fn, "SSL_set_fd");
        load_sym(ssl_accept_fn, "SSL_accept");
        load_sym(ssl_read_fn, "SSL_read");
        load_sym(ssl_write_fn, "SSL_write");
        load_sym(ssl_shutdown_fn, "SSL_shutdown");

        namespace fs = std::filesystem;
        const fs::path cert_dir = fs::temp_directory_path() / "litebird_phase492_proxy_certs";
        fs::remove_all(cert_dir);
        fs::create_directories(cert_dir);
        const fs::path good_key = cert_dir / "good.key";
        const fs::path good_crt = cert_dir / "good.crt";
        assert(std::system(("openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes "
                            "-keyout \"" + good_key.string() + "\" -out \"" + good_crt.string() + "\" "
                            "-days 2 -subj \"/CN=localhost\" -addext \"subjectAltName=IP:127.0.0.1,DNS:localhost\" >/dev/null 2>&1").c_str()) == 0);

        ResourceLoader::set_ca_bundle_for_testing(good_crt.string());
        WebSocketObject::set_ca_bundle_for_testing(good_crt.string());

        void* sctx = ctx_new_fn(tls_server_method_fn());
        assert(sctx != nullptr);
        assert(ctx_use_cert_fn(sctx, good_crt.string().c_str(), 1) == 1);
        assert(ctx_use_key_fn(sctx, good_key.string().c_str(), 1) == 1);

        uint16_t tls_port = 0;
        const int tls_lfd = make_listener(tls_port);
        std::atomic<bool> saw_inner_proxy_auth{false};
        std::atomic<int> tls_http_hits{0};

        std::thread tls_srv([&]() {
            // Serve: 1 HTTPS GET (load_for_page) + 1 WSS echo.
            for (int conn_i = 0; conn_i < 2; ++conn_i) {
                pollfd pfd{tls_lfd, POLLIN, 0};
                if (::poll(&pfd, 1, 5000) <= 0) break;
                const int cfd = ::accept(tls_lfd, nullptr, nullptr);
                if (cfd < 0) break;
                timeval tv{2, 0};
                ::setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                void* s = ssl_new_fn(sctx);
                ssl_set_fd_fn(s, cfd);
                if (ssl_accept_fn(s) == 1) {
                    std::string req;
                    char buf[256];
                    while (req.find("\r\n\r\n") == std::string::npos) {
                        const int r = ssl_read_fn(s, buf, sizeof(buf));
                        if (r <= 0) break;
                        req.append(buf, r);
                    }
                    if (!extract_header_ci(req, "proxy-authorization").empty()) {
                        saw_inner_proxy_auth.store(true);
                    }
                    if (!extract_header_ci(req, "sec-websocket-key").empty()) {
                        const std::string key = extract_header_ci(req, "sec-websocket-key");
                        const std::string up =
                            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Accept: " + sha1_accept(key) + "\r\n\r\n";
                        ssl_write_fn(s, up.data(), static_cast<int>(up.size()));
                        uint8_t wh[2]{};
                        if (ssl_read_fn(s, wh, 2) == 2) {
                            const bool masked = (wh[1] & 0x80) != 0;
                            const std::size_t plen = wh[1] & 0x7f;
                            uint8_t mask[4]{};
                            if (masked) (void)ssl_read_fn(s, mask, 4);
                            std::string msg(plen, '\0');
                            if (plen > 0) (void)ssl_read_fn(s, msg.data(), static_cast<int>(plen));
                            if (masked) for (std::size_t i = 0; i < plen; ++i) msg[i] ^= static_cast<char>(mask[i % 4]);
                            const std::string out = "wss-echo:" + msg;
                            std::string frame;
                            frame.push_back(static_cast<char>(0x81));
                            frame.push_back(static_cast<char>(out.size()));
                            frame += out;
                            ssl_write_fn(s, frame.data(), static_cast<int>(frame.size()));
                            uint8_t cin[8]{};
                            (void)ssl_read_fn(s, cin, 6);
                            const char cout[4]{static_cast<char>(0x88), 2, 0x03, static_cast<char>(0xe8)};
                            ssl_write_fn(s, cout, 4);
                        }
                    } else {
                        tls_http_hits.fetch_add(1);
                        const std::string ok_resp =
                            "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\n"
                            "Content-Length: 11\r\nConnection: close\r\n\r\ntls-proxied";
                        ssl_write_fn(s, ok_resp.data(), static_cast<int>(ok_resp.size()));
                    }
                }
                ssl_shutdown_fn(s);
                ssl_free_fn(s);
                ::close(cfd);
            }
            ::close(tls_lfd);
        });

        uint16_t https_proxy_port = 0;
        const int proxy_lfd = make_listener(https_proxy_port);
        const std::string proxy_auth = "127.0.0.1:" + std::to_string(https_proxy_port);
        const std::string target_auth = "localhost:" + std::to_string(tls_port);

        std::thread connect_proxy([&]() {
            // Rounds:
            //   0-2: HTTPS CONNECT (407 c1 -> 407 stale=true c2 -> 200)
            //   3-4: HTTPS CONNECT wrong/unauthenticated (407 c3 -> 407 refusal, never reaches TLS!)
            //   5-6: WSS CONNECT (407 c4 -> 200)
            for (int step = 0; step < 7; ++step) {
                pollfd pfd{proxy_lfd, POLLIN, 0};
                if (::poll(&pfd, 1, 5000) <= 0) break;
                const int cfd = ::accept(proxy_lfd, nullptr, nullptr);
                if (cfd < 0) break;
                timeval tv{2, 0};
                ::setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                std::string raw;
                if (!read_http_head(cfd, raw)) { ::close(cfd); break; }
                const std::string pa = extract_header_ci(raw, "proxy-authorization");

                if (step == 0) {
                    assert(pa.empty());
                    assert(raw.rfind("CONNECT " + target_auth + " HTTP/1.1\r\n", 0) == 0);
                    send_all_fd(cfd,
                        "HTTP/1.1 407 Proxy Authentication Required\r\n"
                        "Proxy-Authenticate: Digest realm=\"tlsgw\", algorithm=SHA-256, qop=\"auth\", nonce=\"c1\"\r\n"
                        "Content-Length: 0\r\nConnection: close\r\n\r\n");
                    ::close(cfd);
                } else if (step == 1) {
                    assert(verify_digest(pa, "CONNECT", target_auth, "c1", "tlsgw", "tuser", "tpass"));
                    send_all_fd(cfd,
                        "HTTP/1.1 407 Proxy Authentication Required\r\n"
                        "Proxy-Authenticate: Digest realm=\"tlsgw\", algorithm=SHA-256, qop=\"auth\", nonce=\"c2\", stale=true\r\n"
                        "Content-Length: 0\r\nConnection: close\r\n\r\n");
                    ::close(cfd);
                } else if (step == 2) {
                    assert(verify_digest(pa, "CONNECT", target_auth, "c2", "tlsgw", "tuser", "tpass"));
                    int up = ::socket(AF_INET, SOCK_STREAM, 0);
                    sockaddr_in dst{};
                    dst.sin_family = AF_INET;
                    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    dst.sin_port = htons(tls_port);
                    assert(::connect(up, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) == 0);
                    send_all_fd(cfd, "HTTP/1.1 200 Connection Established\r\nProxy-Authentication-Info: nextnonce=\"c3\"\r\n\r\n");
                    relay_pair(cfd, up);
                    ::close(up);
                    ::close(cfd);
                } else if (step == 3) {
                    assert(pa.empty());
                    send_all_fd(cfd,
                        "HTTP/1.1 407 Proxy Authentication Required\r\n"
                        "Proxy-Authenticate: Digest realm=\"tlsgw\", algorithm=SHA-256, qop=\"auth\", nonce=\"bad1\"\r\n"
                        "Content-Length: 0\r\nConnection: close\r\n\r\n");
                    ::close(cfd);
                } else if (step == 4) {
                    assert(!pa.empty());
                    send_all_fd(cfd,
                        "HTTP/1.1 407 Proxy Authentication Required\r\n"
                        "Proxy-Authenticate: Digest realm=\"tlsgw\", algorithm=SHA-256, qop=\"auth\", nonce=\"bad2\"\r\n"
                        "Content-Length: 0\r\nConnection: close\r\n\r\n");
                    ::close(cfd);
                } else if (step == 5) {
                    assert(pa.empty());
                    send_all_fd(cfd,
                        "HTTP/1.1 407 Proxy Authentication Required\r\n"
                        "Proxy-Authenticate: Digest realm=\"tlsgw\", algorithm=SHA-256, qop=\"auth\", nonce=\"w1\"\r\n"
                        "Content-Length: 0\r\nConnection: close\r\n\r\n");
                    ::close(cfd);
                } else {
                    assert(verify_digest(pa, "CONNECT", target_auth, "w1", "tlsgw", "wuser", "wpass"));
                    int up = ::socket(AF_INET, SOCK_STREAM, 0);
                    sockaddr_in dst{};
                    dst.sin_family = AF_INET;
                    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    dst.sin_port = htons(tls_port);
                    assert(::connect(up, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) == 0);
                    send_all_fd(cfd, "HTTP/1.1 200 Connection Established\r\n\r\n");
                    relay_pair(cfd, up);
                    ::close(up);
                    ::close(cfd);
                }
            }
            ::close(proxy_lfd);
        });

        ::setenv("HTTPS_PROXY", ("http://" + proxy_auth).c_str(), 1);
        ResourceLoader::clear_proxy_credentials();
        ResourceLoader::clear_http_connection_pool();
        ResourceLoader::set_proxy_credentials(proxy_auth, "tlsgw", "tuser", "tpass");

        ResourceRequestContext ctx;
        ctx.document_url = "https://localhost:" + std::to_string(tls_port) + "/app.html";
        const ResourceResponse ok_tls = ResourceLoader::load_for_page(
            "https://" + target_auth + "/secure", ctx, "GET",
            {{"Proxy-Authorization", "Basic leak"}}, "");
        assert(ok_tls.network_ok && ok_tls.status_code == 200 && ok_tls.body == "tls-proxied");

        // Wrong proxy credential must fail closed on 407 BEFORE TLS and forget itself.
        ResourceLoader::clear_http_connection_pool();
        ResourceLoader::set_proxy_credentials(proxy_auth, "tlsgw", "tuser", "wrong");
        const ResourceResponse refused_tls = ResourceLoader::load_http("https://" + target_auth + "/blocked", "GET", {}, "");
        assert(!refused_tls.network_ok && !refused_tls.success);
        assert(refused_tls.status_code == 407 && refused_tls.proxy_auth_realm == "tlsgw");
        assert(refused_tls.error_message == "HTTPS proxy CONNECT failed (407)");
        assert(!ResourceLoader::has_proxy_credentials(proxy_auth, "tlsgw"));

        // WSS CONNECT with Digest 407 via HTTPS_PROXY
        ResourceLoader::set_proxy_credentials(proxy_auth, "tlsgw", "wuser", "wpass");
        TestPage page;
        JsInterpreter& js = page.js;
        js.eval(
            "var wssOut = '', wssClose = '';"
            "var ws = new WebSocket('wss://" + target_auth + "/wss-digest');"
            "ws.onopen = function() { ws.send('hello-wss'); };"
            "ws.onmessage = function(e) { wssOut = e.data; ws.close(1000); };"
            "ws.onclose = function(e) { wssClose = e.code + ':' + e.wasClean; };"
        );
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (js.global_env()->get("wssClose").to_string().empty() &&
               std::chrono::steady_clock::now() < deadline) {
            EventLoop::default_loop().tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(js.global_env()->get("wssOut").to_string() == "wss-echo:hello-wss");
        assert(js.global_env()->get("wssClose").to_string() == "1000:true");

        connect_proxy.join();
        tls_srv.join();
        assert(!saw_inner_proxy_auth.load());
        assert(tls_http_hits.load() == 1);

        ctx_free_fn(sctx);
        fs::remove_all(cert_dir);
        ::unsetenv("HTTPS_PROXY");
    }

    // -----------------------------------------------------------------------
    // 3. Plain ws:// via HTTP_PROXY + CONNECT Digest 407 + stale=true +
    //    refused credential erasure + HTTPS_PROXY ignored for ws://
    // -----------------------------------------------------------------------
    {
        uint16_t ws_port = 0;
        const int ws_lfd = make_listener(ws_port);
        std::atomic<int> ws_origins_hit{0};
        std::thread ws_origin([&]() {
            pollfd pfd{ws_lfd, POLLIN, 0};
            if (::poll(&pfd, 1, 4000) > 0) {
                const int cfd = ::accept(ws_lfd, nullptr, nullptr);
                if (cfd >= 0) {
                    ws_origins_hit.fetch_add(1);
                    timeval tv{2, 0};
                    ::setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                    serve_ws_echo(cfd, "plain-ws-msg", "ws-echo:");
                    ::close(cfd);
                }
            }
            ::close(ws_lfd);
        });

        uint16_t ws_proxy_port = 0;
        const int wsp_lfd = make_listener(ws_proxy_port);
        const std::string wsp_auth = "127.0.0.1:" + std::to_string(ws_proxy_port);
        const std::string ws_target = "127.0.0.1:" + std::to_string(ws_port);

        std::thread ws_proxy([&]() {
            // Rounds:
            //   0-2: ws:// CONNECT Digest (407 p1 -> 407 stale=true p2 -> 200 -> ws_origin)
            //   3-4: ws:// CONNECT with wrong password (407 bad1 -> 407 refusal; must forget credential)
            for (int step = 0; step < 5; ++step) {
                pollfd pfd{wsp_lfd, POLLIN, 0};
                if (::poll(&pfd, 1, 4000) <= 0) break;
                const int cfd = ::accept(wsp_lfd, nullptr, nullptr);
                if (cfd < 0) break;
                timeval tv{2, 0};
                ::setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                std::string raw;
                if (!read_http_head(cfd, raw)) { ::close(cfd); break; }
                const std::string pa = extract_header_ci(raw, "proxy-authorization");

                if (step == 0) {
                    assert(pa.empty());
                    assert(raw.rfind("CONNECT " + ws_target + " HTTP/1.1\r\n", 0) == 0);
                    send_all_fd(cfd,
                        "HTTP/1.1 407 Proxy Authentication Required\r\n"
                        "Proxy-Authenticate: Digest realm=\"wsgw\", algorithm=SHA-256, qop=\"auth\", nonce=\"p1\"\r\n"
                        "Content-Length: 0\r\nConnection: close\r\n\r\n");
                    ::close(cfd);
                } else if (step == 1) {
                    assert(verify_digest(pa, "CONNECT", ws_target, "p1", "wsgw", "wsuser", "wspass"));
                    send_all_fd(cfd,
                        "HTTP/1.1 407 Proxy Authentication Required\r\n"
                        "Proxy-Authenticate: Digest realm=\"wsgw\", algorithm=SHA-256, qop=\"auth\", nonce=\"p2\", stale=true\r\n"
                        "Content-Length: 0\r\nConnection: close\r\n\r\n");
                    ::close(cfd);
                } else if (step == 2) {
                    assert(verify_digest(pa, "CONNECT", ws_target, "p2", "wsgw", "wsuser", "wspass"));
                    int up = ::socket(AF_INET, SOCK_STREAM, 0);
                    sockaddr_in dst{};
                    dst.sin_family = AF_INET;
                    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    dst.sin_port = htons(ws_port);
                    assert(::connect(up, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) == 0);
                    send_all_fd(cfd, "HTTP/1.1 200 Connection Established\r\n\r\n");
                    relay_pair(cfd, up);
                    ::close(up);
                    ::close(cfd);
                } else if (step == 3) {
                    assert(pa.empty());
                    send_all_fd(cfd,
                        "HTTP/1.1 407 Proxy Authentication Required\r\n"
                        "Proxy-Authenticate: Digest realm=\"wsgw\", algorithm=SHA-256, qop=\"auth\", nonce=\"p3\"\r\n"
                        "Content-Length: 0\r\nConnection: close\r\n\r\n");
                    ::close(cfd);
                } else {
                    assert(!pa.empty());
                    send_all_fd(cfd,
                        "HTTP/1.1 407 Proxy Authentication Required\r\n"
                        "Proxy-Authenticate: Digest realm=\"wsgw\", algorithm=SHA-256, qop=\"auth\", nonce=\"p4\"\r\n"
                        "Content-Length: 0\r\nConnection: close\r\n\r\n");
                    ::close(cfd);
                }
            }
            ::close(wsp_lfd);
        });

        // HTTPS_PROXY points to a dead port to prove plain ws:// uses HTTP_PROXY, never HTTPS_PROXY.
        ::setenv("HTTPS_PROXY", "http://127.0.0.1:1", 1);
        ::setenv("HTTP_PROXY", ("http://" + wsp_auth).c_str(), 1);
        ResourceLoader::clear_proxy_credentials();
        ResourceLoader::set_proxy_credentials(wsp_auth, "wsgw", "wsuser", "wspass");

        TestPage page;
        JsInterpreter& js = page.js;
        js.eval(
            "var wsRes = '', wsClose = '';"
            "var s = new WebSocket('ws://" + ws_target + "/proxy-digest');"
            "s.onopen = function() { s.send('plain-ws-msg'); };"
            "s.onmessage = function(e) { wsRes = e.data; s.close(1000); };"
            "s.onclose = function(e) { wsClose = e.code + ':' + e.wasClean; };"
        );
        const auto d1 = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (js.global_env()->get("wsClose").to_string().empty() &&
               std::chrono::steady_clock::now() < d1) {
            EventLoop::default_loop().tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(js.global_env()->get("wsRes").to_string() == "ws-echo:plain-ws-msg");
        assert(js.global_env()->get("wsClose").to_string() == "1000:true");

        // Refused credential must fail closed and be erased.
        ResourceLoader::set_proxy_credentials(wsp_auth, "wsgw", "wsuser", "wrong");
        js.eval(
            "var badErr = '', badClose = '';"
            "var s2 = new WebSocket('ws://" + ws_target + "/bad-cred');"
            "s2.onerror = function(e) { badErr = e.message || ''; };"
            "s2.onclose = function(e) { badClose = e.code + ':' + e.wasClean; };"
        );
        const auto d2 = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (js.global_env()->get("badClose").to_string().empty() &&
               std::chrono::steady_clock::now() < d2) {
            EventLoop::default_loop().tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(js.global_env()->get("badErr").to_string() == "WebSocket HTTP proxy CONNECT failed (407)");
        assert(js.global_env()->get("badClose").to_string() == "1006:false");
        assert(!ResourceLoader::has_proxy_credentials(wsp_auth, "wsgw"));

        ws_proxy.join();
        ws_origin.join();
        assert(ws_origins_hit.load() == 1);
    }
}
