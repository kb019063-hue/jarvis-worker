// Phase 434, finding 434: a proxy's `407` is answered in `Proxy-Authorization`,
// out of a protection space of its own.
//
// RFC 9110 §11.7.1 gives a proxy `Proxy-Authenticate` on a `407` and the
// request `Proxy-Authorization`; §11.7 keeps its space apart from the origin's
// so neither can collect the other's credential. Before this phase the loader
// only *reported* the challenge -- `proxy_auth_schemes`/`proxy_auth_realm` --
// and returned the `407` as a failure, so a proxy that wanted credentials could
// never be satisfied.
//
// The suite holds both halves at once, because the half that is easy to get
// wrong is the negative one: a credential the *document* set must not be sent
// to a proxy even when the realm names match exactly.
TEST(net_proxy_authorization_answers_a_407_from_its_own_space) {
    using namespace litebird;

    // base64("Aladdin:open sesame"), RFC 7617 §2's own example credential.
    const std::string kRight = "Proxy-Authorization: Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==";

    struct Seen {
        bool proxy_authorization{false};
        bool document_authorization{false};
        bool right_credential{false};
    };

    // A loopback stand-in for a proxy: `407` with a Basic challenge until the
    // right `Proxy-Authorization` arrives, then `200`. It accepts at most
    // `rounds` connections and gives each a deadline, so a loader that never
    // answers cannot hang the suite.
    auto serve = [&](int rounds, Seen& seen) {
        const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(listener >= 0);
        int reuse = 1;
        ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        assert(::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        assert(::listen(listener, rounds) == 0);
        socklen_t size = sizeof(address);
        assert(::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0);
        const int port = ntohs(address.sin_port);

        std::thread server([listener, rounds, &seen, kRight] {
            for (int round = 0; round < rounds; ++round) {
                pollfd waiting{listener, POLLIN, 0};
                if (::poll(&waiting, 1, 2000) <= 0) return;
                const int client = ::accept(listener, nullptr, nullptr);
                if (client < 0) return;
                timeval deadline{2, 0};
                ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &deadline, sizeof(deadline));
                std::string request;
                char one = 0;
                while (request.size() < 8192 && ::recv(client, &one, 1, 0) == 1) {
                    request += one;
                    if (request.size() >= 4 && request.compare(request.size() - 4, 4, "\r\n\r\n") == 0) break;
                }
                if (request.find(kRight) != std::string::npos) seen.right_credential = true;
                if (request.find("Proxy-Authorization:") != std::string::npos) seen.proxy_authorization = true;
                // The origin's header is what must never appear: match on the
                // line start so the proxy one's own name cannot satisfy it.
                if (request.find("\r\nAuthorization:") != std::string::npos) seen.document_authorization = true;
                const std::string reply = seen.right_credential
                    ? "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nhi"
                    : "HTTP/1.1 407 Proxy Authentication Required\r\n"
                      "Proxy-Authenticate: Basic realm=\"gateway\"\r\n"
                      "Content-Length: 0\r\nConnection: close\r\n\r\n";
                (void)::send(client, reply.data(), reply.size(), MSG_NOSIGNAL);
                ::close(client);
            }
        });
        server.detach();
        return "127.0.0.1:" + std::to_string(port);
    };

    ResourceLoader::clear_http_cache();

    // ---- a proxy credential answers the challenge, once --------------------
    // Nothing is sent pre-emptively: the first request carries no header, the
    // `407` is read, and the retry carries `Proxy-Authorization`. The origin's
    // space contributes nothing, so `Authorization` never appears.
    {
        Seen seen;
        const std::string authority = serve(2, seen);
        ResourceLoader::clear_auth_credentials();
        ResourceLoader::clear_proxy_credentials();
        ResourceLoader::set_proxy_credentials(authority, "gateway", "Aladdin", "open sesame");

        const ResourceResponse resp =
            ResourceLoader::load_http("http://" + authority + "/page", "GET", {}, "");
        assert(resp.status_code == 200);
        assert(resp.body == "hi");
        assert(seen.proxy_authorization);
        assert(seen.right_credential);
        assert(!seen.document_authorization);
        // A `407` that the credential answered is not a refusal: it is kept.
        assert(ResourceLoader::has_proxy_credentials(authority, "gateway"));
    }

    // ---- the negative half: the document's credential is not the proxy's ---
    // The same origin, the same realm name, a credential the *server* would
    // take. It is not offered, because RFC 9110 §11.7 keeps the two spaces
    // apart -- and the `407` is still reported for the embedder, unchanged.
    {
        Seen seen;
        const std::string authority = serve(1, seen);
        ResourceLoader::clear_auth_credentials();
        ResourceLoader::clear_proxy_credentials();
        ResourceLoader::set_auth_credentials("http://" + authority, "gateway", "Aladdin", "open sesame");

        const ResourceResponse resp =
            ResourceLoader::load_http("http://" + authority + "/page", "GET", {}, "");
        assert(resp.status_code == 407);
        assert(!seen.proxy_authorization);
        assert(!seen.document_authorization);
        assert(resp.proxy_auth_schemes.size() == 1);
        assert(resp.proxy_auth_schemes[0] == "Basic");
        assert(resp.proxy_auth_realm == "gateway");
        // Reporting is not answering: the document's credential is untouched,
        // because a `407` was never its business.
        assert(ResourceLoader::has_credentials("http://" + authority, "gateway"));
        // And the proxy space is exactly as empty as it was.
        assert(ResourceLoader::proxy_credential_count() == 0);
    }

    // ---- a refused proxy credential is forgotten ---------------------------
    // A non-stale `407` after the attempt means the proxy refused the pair.
    // Keeping it would replay a wrong password for the life of the process.
    {
        Seen seen;
        const std::string authority = serve(2, seen);
        ResourceLoader::clear_auth_credentials();
        ResourceLoader::clear_proxy_credentials();
        ResourceLoader::set_proxy_credentials(authority, "gateway", "Aladdin", "wrong");

        const ResourceResponse resp =
            ResourceLoader::load_http("http://" + authority + "/page", "GET", {}, "");
        assert(resp.status_code == 407);
        assert(seen.proxy_authorization);            // it was tried...
        assert(!seen.right_credential);              // ...and it was wrong
        assert(!ResourceLoader::has_proxy_credentials(authority, "gateway"));
        assert(ResourceLoader::proxy_credential_count() == 0);
    }

    // ---- the two stores are cleared apart ---------------------------------
    {
        ResourceLoader::clear_auth_credentials();
        ResourceLoader::clear_proxy_credentials();
        ResourceLoader::set_proxy_credentials("127.0.0.1:9", "gateway", "u", "p");
        ResourceLoader::set_auth_credentials("http://127.0.0.1:9", "gateway", "u", "p");
        assert(ResourceLoader::proxy_credential_count() == 1);
        assert(ResourceLoader::auth_credential_count() == 1);

        // Clearing the document's leaves the proxy's, which is the point of
        // having two.
        ResourceLoader::clear_auth_credentials();
        assert(ResourceLoader::auth_credential_count() == 0);
        assert(ResourceLoader::proxy_credential_count() == 1);

        ResourceLoader::clear_proxy_credentials();
        assert(ResourceLoader::proxy_credential_count() == 0);
    }
}
