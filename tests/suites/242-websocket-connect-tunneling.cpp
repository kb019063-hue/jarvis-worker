// Phase 242, finding 246: an invalid configured proxy must not become a direct connection.

TEST(websocket_invalid_http_proxy_fails_closed) {
    using namespace litebird;

    struct EnvBackup {
        std::vector<std::pair<std::string, std::string>> values;
        std::vector<std::string> absent;

        explicit EnvBackup(std::vector<std::string> names) {
            for (const std::string& name : names) {
                const char* current = std::getenv(name.c_str());
                if (current) values.emplace_back(name, current);
                else absent.push_back(name);
            }
        }
        ~EnvBackup() {
            for (const auto& [name, value] : values) ::setenv(name.c_str(), value.c_str(), 1);
            for (const std::string& name : absent) ::unsetenv(name.c_str());
        }
    } env({"HTTPS_PROXY", "https_proxy", "NO_PROXY", "no_proxy", "ALL_PROXY", "all_proxy"});

    ::setenv("https_proxy", "", 1);
    ::setenv("NO_PROXY", "", 1);
    ::setenv("no_proxy", "", 1);
    ::setenv("ALL_PROXY", "", 1);
    ::setenv("all_proxy", "", 1);

    TestPage page;
    JsInterpreter& js = page.js;
    const std::vector<std::string> invalid_proxies = {
        "socks5://127.0.0.1:1080",
        // Empty user/password fields still contain forbidden userinfo syntax.
        "http://:@127.0.0.1:3128",
    };
    for (const std::string& invalid_proxy : invalid_proxies) {
        ::setenv("HTTPS_PROXY", invalid_proxy.c_str(), 1);
        js.eval(R"JS(
            var proxySocketError = '';
            var proxySocketClose = '';
            var proxySocket = new WebSocket('wss://192.0.2.1:443/invalid-proxy');
            proxySocket.onerror = function(e) { proxySocketError = e.message || ''; };
            proxySocket.onclose = function(e) { proxySocketClose = e.code + ':' + e.wasClean; };
        )JS");

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (js.global_env()->get("proxySocketClose").to_string().empty() &&
               std::chrono::steady_clock::now() < deadline) {
            EventLoop::default_loop().tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        assert(js.global_env()->get("proxySocketError").to_string() == "Invalid WebSocket HTTP proxy URL");
        assert(js.global_env()->get("proxySocketClose").to_string() == "1006:false");
    }
}
