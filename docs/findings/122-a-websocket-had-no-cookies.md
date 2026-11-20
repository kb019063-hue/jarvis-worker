# Finding 122 — a WebSocket had no cookies

Phase 117. The architecture and the measurements are in
[`docs/phases/117-cookies-on-the-websocket-upgrade.md`](../phases/117-cookies-on-the-websocket-upgrade.md).
Status: **resolved** for the upgrade request and its response; what is left of
the row is binary frames and extensions.

## 1. What was wrong

Two greps said it in one line each:

```bash
grep -c "Cookie"     src/net/WebSocket.cpp   # 0
grep -c "Set-Cookie" src/net/WebSocket.cpp   # 0
```

The engine had held cookies in one `CookieJar` since Phase 36 and used it for
every `fetch`, every subresource and `document.cookie` itself. The RFC 6455
upgrade was the one request that skipped it — a hand-built header block of
`GET`, `Host`, `Upgrade`, `Connection`, `Origin` and three `Sec-WebSocket-*`
fields.

So a page that authenticated with a cookie could not open a socket to the
endpoint that cookie was for. The server saw an anonymous client and answered
`401`, and the page saw `error` then `close` with code 1006 and no way to tell
a rejected session from a dead server. The reverse was broken the same way: a
`Set-Cookie` on the `101` — the usual place a handshake hands out a session —
was parsed as an unknown header and dropped.

That the row knew is in `AGENTS.md` since Phase 90: *"Open: … `Cookie`/
`Set-Cookie` forwarding on upgrades"*.

## 2. What the fix was

- `m_cookie_url` is the upgrade URL with the `ws`/`wss` scheme written as
  `http`/`https`, and `m_cookie_site` is the document URL (empty when there is
  no document). Both are computed once, in the constructor, next to `m_path`.
- The request asks `CookieJar::default_jar().get_cookie_header(m_cookie_url,
  m_cookie_site, /*is_top_level_navigation=*/false, "GET")` and adds the answer
  as one `Cookie:` line. All the matching rules — domain, path, expiry,
  `Secure`, `SameSite` — stay in the jar, the same code `fetch` calls.
- The response loop collects `set-cookie` lines individually (a `101` may set
  several) and stores each with `set_cookie_from_header(..., m_cookie_url)`
  before the `101` is validated, as an HTTP client stores cookies from a
  response it has already received.

`ws` matched as `http` and `wss` as `https` is the entire scheme problem,
because `Secure` is the only cookie attribute a scheme decides. The mapping is
kept in one expression with the reason next to it, rather than in a second
cookie matcher.

## 3. What is still open

| What | Why it is left |
| :--- | :--- |
| No per-call credentials mode | `new WebSocket()` has no `credentials` option; the browser default (send, filtered by the jar) is the one taken |
| `partitioned`/CHIPS cookies | the jar does not partition them for `fetch` either |
| No cookies on a redirect | RFC 6455 has no redirect step in the handshake |
| `httpOnly` cookies are sent, untested from the page | the header path in the jar includes them (the same is asserted for `fetch`); JavaScript cannot read one to prove it from the page side |
| Proxies | the proxy path itself is still unimplemented |
| Binary frames, `permessage-deflate` | the rest of the WebSocket row |

Two oracles, and neither is this code's own output: the jar's rules are the
ones *its own existing tests* hold it to (RFC 6265 domain/path/`Secure`
vectors), and the wire is a loopback server this repository starts, which keeps
the exact request block — plus the engine's own `document.cookie`, read from an
HTTP page after the `101` and from an HTTPS page for the `Secure` case.
