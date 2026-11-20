# Phase 117 — Cookies on the WebSocket upgrade

Task row: **WebSocket `ArrayBuffer`/`Blob` binary exposure, upgrade cookies, and
extensions remain open.** The row splits three ways, and only the middle third
is in reach: binary frames wait for `ArrayBuffer`/typed arrays (held by pull
request #132), extensions mean `permessage-deflate`, and `src/net/Deflate.cpp`
compress only stored blocks — no Huffman encoder, no dictionary — so that is a
phase of its own. Cookies on the upgrade were the third that nothing held.
Finding 122.

## 1. What was wrong

The RFC 6455 request was built from six headers, and `Cookie` was not one of
them:

```cpp
    std::string handshake_req =
        "GET " + m_path + " HTTP/1.1\r\n"
        "Host: " + m_host_header + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Origin: " + m_origin_header + "\r\n"
        "Sec-WebSocket-Key: " + sec_key + "\r\n"
        "Sec-WebSocket-Version: 13\r\n";
```

Both greps returned zero:

```bash
grep -c "Cookie"     src/net/WebSocket.cpp   # 0
grep -c "Set-Cookie" src/net/WebSocket.cpp   # 0
```

`fetch` and every subresource load had gone through `CookieJar` since Phase 36,
and `document.cookie` was the same jar, so the engine held the credentials and
then declined to present them on the one request a page makes *after* logging
in. A `wss://` endpoint behind a session cookie answered `401` to a client that
had already been given the cookie.

The row had said so since Phase 90: *"Open: … `Cookie`/`Set-Cookie` forwarding
on upgrades"*.

## 2. What changed

**The request.** Before the handshake is written, the jar is asked the same
question `fetch` asks:

```cpp
std::string m_cookie_url;   // ws://host/path as http://, wss:// as https://
std::string m_cookie_site;  // the page URL, or empty with no document

cookie_val = CookieJar::default_jar().get_cookie_header(m_cookie_url, m_cookie_site, false, "GET");
```

`is_top_level_navigation` is `false`, because a WebSocket is a subresource
request and never a navigation. Domain, path, expiry, `Secure` and `SameSite`
are then the jar's own rules, applied by the code `fetch` already uses: nothing
here re-implements cookie matching.

**The scheme mapping is the whole scheme question.** `get_cookie_header` only
understands `http` and `https`, and `Secure` is the only cookie attribute that
depends on a scheme. So a `ws://127.0.0.1:9000/chat` upgrade is matched as
`http://127.0.0.1:9000/chat` and a `wss://` one as `https://`: a `Secure`
cookie goes out on `wss` and stays behind on `ws`, which is exactly what RFC
6265 §4.1.2.5 asks for, without teaching the jar a third and fourth scheme.
Ports are kept because the jar ignores them, not because the mapping needs
them.

**The response.** A handshake answers with one HTTP response and it is never
read again, so `Set-Cookie` has exactly one chance:

- the header loop now collects every `set-cookie` line separately rather than
  folding them into a map (a 101 can set several cookies, and RFC 6265 §5.3
  stores each);
- each is stored through `set_cookie_from_header(set_cookie, m_cookie_url)`
  before the `101` is validated, the way an HTTP client stores cookies from a
  response it has already received. A rejected upgrade — the wrong
  `Sec-WebSocket-Accept`, a missing `Upgrade`, anything §4.1 of RFC 6455
  refuses — therefore still keeps what the server set, and the connection
  still fails closed.

**Where the site comes from.** `m_cookie_site` is the document URL when there
is a non-opaque document, the same value `fetch` passes as
`site_for_cookies`. With no document (a worker, a `data:` page, the CLI) it is
empty, and the jar's own rule leaves SameSite filtering off rather than
inventing a site.

## 3. How it was verified

`make test` — **273 / 273 suites**, up from 272 on the merged `main`; the new suite is
`websocket_upgrade_sends_and_stores_cookies`. It runs a loopback listener,
seeds the real default jar, and keeps the request block the server received:

| Cookie held for the page's origin | Sent on `ws://…/chat`? | Why |
| :--- | :--- | :--- |
| `ws_pre=1; Path=/` | yes | same host, path matches |
| `ws_same_site=1; Path=/; SameSite=Strict` | yes | set from the page's own site; the upgrade is a subresource of that site |
| `ws_cross_site=1; Path=/; SameSite=Strict` | **no** | set from `other.example`; a non-navigation cross-site request carries no Strict cookie |
| `ws_secure=1; Path=/; Secure` | **no** | `ws` is matched as `http` |
| `ws_other_path=1; Path=/other` | **no** | the URL is `/chat` |

and the response's `Set-Cookie: ws_session=abc; Path=/chat` and
`Set-Cookie: ws_elsewhere=1; Path=/other` are in the jar afterwards, each
against the path it was set for and not the other.

The test was checked by breaking the code it is meant to hold:

```
send disabled      → Assertion `cookie_pos != std::string::npos' failed
store disabled     → Assertion `chat_cookies.find("ws_session=abc")` failed
scheme forced http → `wss:// upgrade sends the page's Secure cookie` FAIL (cookie=None)
```

`make security-websocket` — **32 checks, 0 failures**, up from 29. Three are
new, and they go through the real `new WebSocket()` path rather than the C++
object: an HTTP page sets `document.cookie` and opens `ws://…/ws-cookie`
(passes only if the header reaches the wire), the same page's
`document.cookie` afterwards contains what the `101` set, and an HTTPS page's
`Secure` cookie arrives on a `wss://` upgrade — which is the check the
`http`-forced mutation above breaks.

Nothing else moved:

| Command | Before | After |
| :--- | :--- | :--- |
| `make test` | 272 / 272 | **273 / 273** |
| `make js-conformance` | 1141 / 1158 | 1141 / 1158 |
| `make dom-conformance` | 678 / 678 | 678 / 678 |
| `make conformance` | 1973 / 1973 | 1973 / 1973 |
| `make url-conformance` | 1188 / 1188 | 1188 / 1188 |
| `make wpt` | 9567 / 9824 | 9567 / 9824 |
| `make security-websocket` | 29 checks | **32 checks** |
| `make check-numbers` | 0 failures | 0 failures |
| `make check-docs` | 0 failures | 0 failures |

## 4. What is still open

- **No cookie-credentials policy per call.** The upgrade always sends what the
  jar holds; there is no equivalent of `fetch`'s `credentials: 'omit'` / same-
  origin switch, because `new WebSocket()` has no such option in the API. The
  browser default is the one taken: send, and let domain/path/SameSite decide.
- **`partitioned` / CHIPS cookies** are not partitioned in the jar for `fetch`
  either; the upgrade inherits that.
- **`Cookie` is not sent on a redirect.** The WebSocket handshake does not
  follow redirects (RFC 6455 has no such step), so there is nothing to
  re-attach a header to.
- **`httpOnly` cookies are included**, as they must be: `document.cookie`
  cannot see them, and the upgrade is a network request, not a script. There is
  no test asserting that from the page side, because the jar has no way to read
  an `httpOnly` cookie from JavaScript to prove it.
- **Proxy `Cookie` handling** is untouched, along with the proxies themselves.
- **Binary frames and `permessage-deflate`** are still open, as in §0.
