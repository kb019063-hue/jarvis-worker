# Finding 117 — a `401` was just a failed load

Phase 113. Status: **resolved** for Basic and Digest; the parts left out are
listed in §3 rather than implied to work.

## 1. What was wrong

`ResourceLoader` treated `401 Unauthorized` as any other non-2xx: `success`
false, `status_code` 401, no further information. `WWW-Authenticate` was
neither parsed nor preserved in a form anything could act on, and no request
ever carried `Authorization`.

The practical effect: any page, script or subresource behind HTTP
authentication was a blank body, and the embedder could not tell an
authentication failure from a missing file without re-reading the header
itself.

`ROADMAP.md` knew. Tier 1 row 7 ended with *"Still open: HTTP authentication
challenges (`WWW-Authenticate`/`401`)"*, which made this the last item of an
otherwise-finished row.

## 2. What the fix was

- `auth_parse_challenges` reads an RFC 9110 §11.6.1 field into schemes and
  parameters, tolerating several challenges in one field and quoted values
  containing commas (`qop="auth,auth-int"`).
- `auth_authorization_for` chooses the strongest answerable challenge —
  Digest/SHA-256, Digest/MD5, then Basic — and builds the header **only** when
  a credential is held for that origin *and* that realm.
- `load_http`'s redirect loop retries the same hop once after a `401`, with the
  header added and nothing else changed.
- `ResourceResponse` gained `auth_schemes` and `auth_realm`, so a `401` with no
  credentials held is still informative.

The digests are RFC 1321 MD5 and FIPS 180-4 SHA-256, written out here, and are
held in the test to the RFCs' own published vectors (including all of RFC 7616
§3.9.1 and §3.9.2) rather than to their own output.

## 3. What is still open

| What | Why it is left |
| :--- | :--- |
| `auth-int` | hashes the entity body; needs the body at the point the header is built |
| `SHA-512-256` | refused by name rather than approximated |
| `userhash=true`, non-UTF-8 `charset` | both change what the octets mean |
| `Authentication-Info`/`nextnonce` | a rotating nonce costs one extra round trip |
| `407`/`Proxy-Authenticate` | proxy authentication is a separate path |
| A refused credential stays stored | the load fails after one retry; the entry is not deleted between loads |
| No prompt | credentials come from `set_auth_credentials`; a terminal shell has no dialog |

The one divergence worth naming: the oracle for the *wire* is a loopback server
this repository starts, so the exchange is genuinely this engine's bytes. The
oracle for the *digests* is the RFCs' text, which is independent of both. What
is not independently checked is a real server's opinion of a real exchange —
that needs an authenticated endpoint, and there is none in the test tree.
