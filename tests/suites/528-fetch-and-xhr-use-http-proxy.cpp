// Phase 528, finding 528: Fetch and XMLHttpRequest share ResourceLoader's
// HTTP_PROXY route and send absolute-form targets through the configured proxy.
TEST(fetch_and_xhr_use_the_http_proxy) {
    using namespace litebird;

    struct EnvGuard {
        std::vector<std::pair<std::string, std::string>> previous;
        std::vector<std::string> absent;
        EnvGuard() {
            for (const char* key : {"HTTP_PROXY", "http_proxy", "HTTPS_PROXY", "https_proxy",
                                    "ALL_PROXY", "all_proxy", "NO_PROXY", "no_proxy"}) {
                if (const char* value = std::getenv(key)) previous.emplace_back(key, value);
                else absent.emplace_back(key);
                ::unsetenv(key);
            }
        }
        ~EnvGuard() {
            for (const auto& [key, value] : previous) ::setenv(key.c_str(), value.c_str(), 1);
            for (const auto& key : absent) ::unsetenv(key.c_str());
        }
    } env_guard;

    struct LoaderGuard {
        ~LoaderGuard() {
            ResourceLoader::clear_http_cache();
            ResourceLoader::clear_http_connection_pool();
        }
    } loader_guard;
    ResourceLoader::clear_http_cache();
    ResourceLoader::clear_http_connection_pool();

    struct SeenRequest {
        std::string method;
        std::string target;
        std::string body;
    };
    std::mutex requests_mutex;
    std::vector<SeenRequest> requests;
    LoopbackHttpTestServer proxy([&](const HttpServerRequest& request, HttpServerResponse& response) {
        {
            std::lock_guard<std::mutex> lock(requests_mutex);
            requests.push_back({request.method, request.raw_target, request.body});
        }
        response.status = 200;
        response.content_type = "text/plain";
        response.headers.push_back({"Access-Control-Allow-Origin", "*"});
        response.body = request.raw_target;
    });

    const std::string proxy_url = "http://127.0.0.1:" + std::to_string(proxy.port);
    ::setenv("HTTP_PROXY", proxy_url.c_str(), 1);

    const std::string fetch_url = "http://target.invalid/from-fetch?q=1";
    const std::string xhr_url = "http://target.invalid/from-xhr?q=2";
    TestPage page(TestPage::WithUrl{});
    page.document().set_url("http://page.invalid/index.html");
    page.load("<!doctype html><body></body>");
    JsInterpreter& js = page.js;
    js.eval(
        "var proxyResults = {};"
        "fetch('" + fetch_url + "', { method: 'POST', body: 'fetch-payload' })"
        ".then(function(r) { return r.text(); }).then(function(t) {"
        "  proxyResults.fetch = t;"
        "}, function(e) { proxyResults.fetchError = String(e); });"
        "var proxyXhr = new XMLHttpRequest();"
        "proxyXhr.open('POST', '" + xhr_url + "', true);"
        "proxyXhr.onload = function() { proxyResults.xhr = proxyXhr.responseText; };"
        "proxyXhr.onerror = function() { proxyResults.xhrError = 'network error'; };"
        "proxyXhr.send('xhr-payload');");

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        EventLoop::default_loop().run_until_idle(std::chrono::milliseconds(50));
        if (js.eval("Boolean(proxyResults.fetch && (proxyResults.xhr || proxyResults.xhrError))").is_truthy()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    assert(js.eval("proxyResults.fetch").to_string() == fetch_url);
    assert(js.eval("proxyResults.xhr").to_string() == xhr_url);
    {
        std::lock_guard<std::mutex> lock(requests_mutex);
        assert(requests.size() == 2u);
        auto has_request = [&](const std::string& target, const std::string& body) {
            return std::any_of(requests.begin(), requests.end(), [&](const SeenRequest& request) {
                return request.method == "POST" && request.target == target && request.body == body;
            });
        };
        assert(has_request(fetch_url, "fetch-payload"));
        assert(has_request(xhr_url, "xhr-payload"));
    }

    proxy.stop();
}
